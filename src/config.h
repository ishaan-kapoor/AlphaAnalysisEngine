#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "alpha_params.h"

namespace hft {

struct AlphaSpec {
    std::string source;
    std::string name;
    AlphaParams params;
};

// The value of an alpha channel at t+delay, written into the row sampled at t.
// transform "return" emits (future-current)/current; "raw" emits the future value.
struct LeadSpec {
    std::string   source;
    std::string   channel;
    std::uint64_t delayNs = 0;
    std::uint64_t delayTicks = 0;
    bool          isReturn = false; // transform: return vs raw
    std::string   name;
};

struct RunConfig {
    std::vector<std::string> symbols;  // instruments to monitor
    std::vector<AlphaSpec>   alphas;   // alphas, in order
    std::size_t              samplingRate = 1; // write one row every N alpha-updating events per symbol
    std::string              output;  // output dir
    std::vector<LeadSpec>    leads;  // alpha channel at t+delay
    bool                     rawFeed = false;  // write per-symbol input events
    bool                     orderBook = false;  // write per-symbol order book
    std::size_t              orderBookDepth = 20;  // only relevant while dumping order book
    std::uint64_t            startNs = 0;  // output window as "HH:MM:SS[:nnnnnnnnn]" time-of-day, parsed to ns-since-midnight
    std::uint64_t            endNs = std::numeric_limits<std::uint64_t>::max();  // execution stop time
};

RunConfig loadConfig(const std::string& path);

}
