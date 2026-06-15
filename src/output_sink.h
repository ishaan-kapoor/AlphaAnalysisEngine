#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "types.h"

namespace hft {

// `values` is valid only for the duration of the onRow() call; copy if deferred.
struct SampleRow {
    std::uint16_t           locate; // per-symbol routing key (ITCH stock-locate)
    std::uint64_t           index;  // raw per-symbol event ordinal (joins the feed/book dumps)
    Ts                      ts;     // exchange timestamp (ns)
    std::span<const double> values; // alpha outputs, in column order
};

class OutputSink {
public:
    virtual ~OutputSink() = default;
    virtual void onHeader(std::uint16_t locate, std::string_view symbol,
                          const std::vector<std::string>& columns) = 0;
    virtual void onRow(const SampleRow& row) = 0;
    virtual void flush() {}
};

}
