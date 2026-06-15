#include "run.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "alpha_registry.h"
#include "csv_sink.h"
#include "inspect.h"
#include "instrument.h"
#include "market_event.h"
#include "order_book.h"
#include "output_sink.h"
#include "sampler.h"

namespace hft {

namespace {

std::string resolveTemplates(std::string s) {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char stamp[32];
  std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &tm);
  const std::string token = "<datetime>";
  const std::string value = stamp;
  for (std::size_t pos = s.find(token); pos != std::string::npos;
       pos = s.find(token, pos + value.size())) {
    s.replace(pos, token.size(), value);
  }
  return s;
}

// Builds the configured alphas into a throwaway instrument so unknown names,
// bad ordering, or a lead pointing at a missing alpha/channel fail clearly
// before the replay starts, not midway.
void validateAlphaConfig(const RunConfig &cfg) {
  const AlphaRegistry &reg = AlphaRegistry::instance();
  Instrument scratch("_validate");
  std::unordered_map<std::string, Alpha *> built;
  AlphaContext ctx{scratch, [&built](const std::string &n) -> Alpha * {
                     std::unordered_map<std::string, Alpha *>::iterator it =
                         built.find(n);
                     return it == built.end() ? nullptr : it->second;
                   }};
  for (const AlphaSpec &spec : cfg.alphas) {
    if (!reg.known(spec.source)) {
      throw std::runtime_error("unknown alpha '" + spec.source + "'");
    }
    if (built.find(spec.name) != built.end()) {
      throw std::runtime_error("duplicate alpha name '" + spec.name + "'");
    }
    ctx.params = spec.params;
    Alpha &a = reg.build(spec.source, ctx);
    built[spec.name] = &a;
  }
  std::unordered_set<std::string> leadNames;
  for (const LeadSpec &lc : cfg.leads) {
    std::unordered_map<std::string, Alpha *>::iterator it =
        built.find(lc.source);
    if (it == built.end()) {
      throw std::runtime_error("lead '" + lc.name + "': source alpha '" +
                               lc.source + "' is not in 'alphas'");
    }
    channelIndex(*it->second, lc.channel); // throws on a bad channel name
    if (!leadNames.insert(lc.name).second) {
      throw std::runtime_error("duplicate lead name '" + lc.name + "'");
    }
  }
}

}

void validateConfig(RunConfig &cfg, const std::string &configPath) {
  validateAlphaConfig(cfg);
  cfg.output = resolveTemplates(cfg.output);
  std::filesystem::create_directories(cfg.output);
  std::error_code ec;
  std::filesystem::copy_file(configPath, cfg.output + "/config.json",
                             std::filesystem::copy_options::overwrite_existing,
                             ec);
  if (ec) { // best-effort: a failed reproducibility copy warns but isn't fatal
    std::fprintf(stderr,
                 "warning: could not copy config to %s/config.json: %s\n",
                 cfg.output.c_str(), ec.message().c_str());
  }
}

// Replays a feed under a config: every symbol gets its own book + the
// configured alphas, computed on EVERY tick.
int run(Feed &feed, const std::string &label, const RunConfig &cfg) {
  if (!feed.valid()) {
    std::printf("cannot open %s\n", label.c_str());
    return 1;
  }

  const std::string &outdir = cfg.output;
  const AlphaRegistry &reg = AlphaRegistry::instance();

  std::unique_ptr<OutputSink> sink = std::make_unique<CsvMultiplexSink>(outdir);

  std::unique_ptr<SystemFeedWriter> sysFeedW;
  if (cfg.rawFeed) {
    sysFeedW = std::make_unique<SystemFeedWriter>(outdir + "/feed.csv");
  }

  struct CfgBook {
    Instrument inst;
    std::uint16_t locate = 0;
    std::size_t events = 0;
    std::size_t updates = 0; // alpha-updating events (sampling base)
    std::size_t crossings = 0;
    std::unique_ptr<SymbolSampler> sampler;
    std::unique_ptr<FeedTraceWriter> feedW;  // rawFeed dump (null if off)
    std::unique_ptr<OrderBookWriter> bookW; // orderBook dump (null if off)
    CfgBook(std::string sym, std::uint16_t loc)
        : inst(std::move(sym)), locate(loc) {}
  };

  std::unordered_map<std::uint16_t, CfgBook> books;

  // Lazily create a symbol's book: build alphas in config order, resolve the
  // lead sources/channels against them, announce the columns, and wire up the
  // sampler.
  auto makeBook = [&](std::uint16_t locate,
                      const std::string &sym) -> CfgBook & {
    std::pair<std::unordered_map<std::uint16_t, CfgBook>::iterator, bool> res =
        books.try_emplace(locate, sym, locate);
    CfgBook &bk = res.first->second;
    std::unordered_map<std::string, Alpha *> built;
    AlphaContext ctx{bk.inst, [&built](const std::string &n) -> Alpha * {
                       std::unordered_map<std::string, Alpha *>::iterator it =
                           built.find(n);
                       return it == built.end() ? nullptr : it->second;
                     }};
    std::vector<std::string> columns;
    std::vector<const Alpha *> alphas;
    for (const AlphaSpec &spec : cfg.alphas) {
      ctx.params = spec.params;
      Alpha &a = reg.build(spec.source, ctx);
      built[spec.name] = &a;
      alphas.push_back(&a);
      for (const std::string &cn : a.outputNames()) {
        columns.push_back(cn.empty() ? spec.name : spec.name + "_" + cn);
      }
    }
    std::vector<ResolvedLead> leads;
    for (const LeadSpec &leadCfg : cfg.leads) {
      ResolvedLead lead;
      lead.src = built.at(leadCfg.source); // presence validated up front
      lead.channel = channelIndex(*lead.src, leadCfg.channel);
      lead.tick = (leadCfg.delayTicks > 0);
      lead.delay = lead.tick ? leadCfg.delayTicks : leadCfg.delayNs;
      lead.isReturn = leadCfg.isReturn;
      leads.push_back(lead);
      columns.push_back(leadCfg.name);
    }
    sink->onHeader(locate, bk.inst.symbol(), columns);
    bk.sampler = std::make_unique<SymbolSampler>(
        locate, std::move(alphas), std::move(leads), *sink);
    // Per-event inspection dumps (independent of the alpha sink/sampler).
    if (cfg.rawFeed) {
      bk.feedW = std::make_unique<FeedTraceWriter>(
          outdir + "/" + bk.inst.symbol() + ".feed.csv");
    }
    if (cfg.orderBook) {
      bk.bookW = std::make_unique<OrderBookWriter>(
          outdir + "/" + bk.inst.symbol() + ".book.csv", cfg.orderBookDepth);
    }
    return bk;
  };

  std::printf("replaying %s\n", label.c_str());
  if (cfg.symbols.empty()) {
    std::printf(
        "config: all symbols, %zu alpha(s), %zu lead(s); samplingRate=%zu\n",
        cfg.alphas.size(), cfg.leads.size(), cfg.samplingRate);
  } else {
    std::printf(
        "config: %zu symbol(s), %zu alpha(s), %zu lead(s); samplingRate=%zu\n",
        cfg.symbols.size(), cfg.alphas.size(), cfg.leads.size(),
        cfg.samplingRate);
  }
  std::printf("writing per-symbol CSV to %s/\n", outdir.c_str());
  if (cfg.rawFeed || cfg.orderBook) {
    std::printf("inspection: rawFeed=%s orderBook=%s depth=%zu "
                "(per-event, ignores samplingRate)\n",
                cfg.rawFeed ? "on" : "off", cfg.orderBook ? "on" : "off",
                cfg.orderBookDepth);
  }
  if (cfg.startNs != 0 ||
      cfg.endNs != std::numeric_limits<std::uint64_t>::max()) {
    std::printf(
        "output window: rows with ts in [%llu, %llu] ns since midnight\n",
        static_cast<unsigned long long>(cfg.startNs),
        static_cast<unsigned long long>(cfg.endNs));
    if (cfg.endNs != std::numeric_limits<std::uint64_t>::max()) {
      std::printf("replay warms from the open, then stops at endTime\n");
    }
  }

  MarketEvent event;
  std::size_t total = 0;
  while (feed.next(event)) {
    // Replay STARTS at the open (before startTime), but ENDS at endTime.
    if (event.ts > cfg.endNs) {
      break;
    }


    if (isSystemWide(event.type)) {
      if (sysFeedW && event.ts >= cfg.startNs) { sysFeedW->write(event); }
      continue;
    }

    std::unordered_map<std::uint16_t, CfgBook>::iterator it = books.find(event.locate);
    CfgBook &bk = (it != books.end())
                      ? it->second
                      : makeBook(event.locate, feed.symbolOf(event.locate));
    const bool updated = bk.inst.onEvent(event);
    ++bk.events;
    ++total;
    if (bk.inst.book().crossed()) {
      ++bk.crossings;
    }

    // Write only rows at/after startTime
    const bool inWindow = (event.ts >= cfg.startNs);

    if (inWindow) {
      if (bk.feedW) {
        bk.feedW->write(bk.events, event);
      }
      if (bk.bookW) {
        bk.bookW->write(bk.events, event.ts, bk.inst.book());
      }
    }

    bk.sampler->advance(event.ts, bk.updates);
    if (updated) {
      ++bk.updates;
      if (inWindow && (bk.updates % cfg.samplingRate == 0)) {
        bk.sampler->sample(bk.events, bk.updates, event.ts);
      }
    }
  }

  // End of data: flush each sampler (unresolved leads emit NaN).
  for (std::pair<const std::uint16_t, CfgBook> &kv : books) {
    kv.second.sampler->flush();
  }

  // Flush the inspection dumps too, so every file is complete on disk before
  // we report the output dir (their destructors would flush at scope exit, but
  // that is after the summary prints).
  for (std::pair<const std::uint16_t, CfgBook> &kv : books) {
    if (kv.second.feedW) { kv.second.feedW->flush(); }
    if (kv.second.bookW) { kv.second.bookW->flush(); }
  }
  if (sysFeedW) { sysFeedW->flush(); }

  sink->flush();
  sink.reset();

  std::size_t totalCross = 0;
  for (const std::pair<const std::uint16_t, CfgBook> &kv : books) { totalCross += kv.second.crossings; }
  if (totalCross == 0) { return 0; }

  std::vector<const CfgBook *> v;
  v.reserve(books.size());
  for (const std::pair<const std::uint16_t, CfgBook> &kv : books) {
    v.push_back(&kv.second);
  }
  std::sort(v.begin(), v.end(), [](const CfgBook *a, const CfgBook *b) {
    return a->events > b->events;
  });

  std::printf(
      "\n=== final: %zu events across %zu symbols ===\n",
      total, books.size());
  std::printf("%-8s %7s %12s %12s %10s\n", "symbol", "locate", "events", "rows",
              "crossings");
  for (const CfgBook *p : v) {
    std::printf("%-8s %7u %12zu %10zu\n", p->inst.symbol().c_str(),
                p->locate, p->events, p->crossings);
  }
  std::printf("INVARIANT VIOLATED: %zu crossed events across all symbols.\n",
              totalCross);
  return 0;
}

}
