#include "config.h"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

namespace hft {

namespace {

// Parses an exchange time-of-day "HH:MM:SS" or "HH:MM:SS:nnnnnnnnn" into
// nanoseconds since midnight. The optional 4th field is a literal nanosecond
// count (0..999999999). ITCH carries no date (a file is a single trading day),
// so a window is always a time-of-day. Throws on a malformed string.
std::uint64_t parseTimeOfDay(const std::string& s) {
    unsigned h = 0, m = 0, sec = 0;
    unsigned long long ns = 0;
    const int n = std::sscanf(s.c_str(), "%u:%u:%u:%llu", &h, &m, &sec, &ns);
    if (n < 3) {
        throw std::runtime_error("bad time '" + s + "' (want HH:MM:SS[:nnnnnnnnn])");
    }
    if (m > 59 || sec > 59 || ns > 999999999ULL) {
        throw std::runtime_error("bad time '" + s + "' (a field is out of range)");
    }
    return (static_cast<std::uint64_t>(h) * 3600 + m * 60 + sec) * 1000000000ULL + ns;
}

}

RunConfig loadConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) { throw std::runtime_error("cannot open config: " + path); }

    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        throw std::runtime_error("config parse error in " + path + ": " + e.what());
    }

    RunConfig cfg;
    if (j.contains("symbols")) {
        for (const nlohmann::json& s : j.at("symbols")) {
            cfg.symbols.push_back(s.get<std::string>());
        }
    }

    if (j.contains("alphas")) {
        for (const nlohmann::json& a : j.at("alphas")) {
            AlphaSpec spec;
            spec.source = a.at("source").get<std::string>();
            spec.name   = a.value("name", spec.source);
            if (a.contains("params")) {
                for (nlohmann::json::const_iterator it = a.at("params").begin();
                     it != a.at("params").end(); ++it) {
                    spec.params.set(it.key(), it.value().get<double>());
                }
            }
            cfg.alphas.push_back(std::move(spec));
        }
    }

    if (j.contains("samplingRate")) {
        cfg.samplingRate = j.at("samplingRate").get<std::size_t>();
    }
    if (cfg.samplingRate == 0) { cfg.samplingRate = 1; }

    if (j.contains("output")) {
        cfg.output = j.at("output").get<std::string>();
    }
    if (cfg.output.empty()) { cfg.output = "out/<datetime>"; }

    // General time-shifted ("lead") label columns.
    if (j.contains("leads")) {
        for (const nlohmann::json& l : j.at("leads")) {
            LeadSpec lead;
            lead.source     = l.at("source").get<std::string>();
            lead.channel    = l.value("channel", std::string{});
            lead.delayNs    = l.value("delayNs", std::uint64_t{0});
            lead.delayTicks = l.value("delayTicks", std::uint64_t{0});
            lead.isReturn   = (l.value("transform", std::string{"raw"}) == "return");
            if ((lead.delayNs == 0) == (lead.delayTicks == 0)) {
                throw std::runtime_error("lead on '" + lead.source +
                    "' needs exactly one of delayNs / delayTicks (> 0)");
            }
            const std::string autoName = lead.delayTicks > 0
                ? lead.source + "_" + std::to_string(lead.delayTicks) + "ticks"
                : lead.source + "_" + std::to_string(lead.delayNs) + "ns";
            lead.name = l.value("name", autoName);
            cfg.leads.push_back(std::move(lead));
        }
    }

    cfg.rawFeed        = j.value("rawFeed", cfg.rawFeed);
    cfg.orderBook      = j.value("orderBook", cfg.orderBook);
    cfg.orderBookDepth = j.value("orderBookDepth", cfg.orderBookDepth);
    if (cfg.orderBookDepth == 0) { cfg.orderBookDepth = 20; }

    if (j.contains("startTime")) {
        cfg.startNs = parseTimeOfDay(j.at("startTime").get<std::string>());
    }
    if (j.contains("endTime")) {
        cfg.endNs = parseTimeOfDay(j.at("endTime").get<std::string>());
    }
    if (cfg.startNs > cfg.endNs) {
        throw std::runtime_error("config: startTime is after endTime");
    }

    // An empty (or omitted) 'symbols' list implies "watch every symbol in the
    // feed"
    if (cfg.alphas.empty()) {
        throw std::runtime_error("config: 'alphas' is empty");
    }
    return cfg;
}

}
