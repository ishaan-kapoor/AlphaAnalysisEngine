#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "csv_buf.h"
#include "output_sink.h"
#include "types.h"

namespace hft {

// Header = event_index,ts_ns then the given channel columns
class CsvRowWriter {
public:
    CsvRowWriter(const std::string& path, const std::vector<std::string>& columns)
        : buf_(path) {
        buf_.appendText("event_index,ts_ns");
        for (const std::string& c : columns) {
            buf_.appendChar(',');
            buf_.appendText(c);
        }
        buf_.endRow();
    }

    bool valid() const { return buf_.valid(); }

    void writeRow(std::uint64_t index, Ts ts, std::span<const double> values) {
        buf_.appendUInt(index);
        buf_.appendChar(',');
        buf_.appendUInt(ts);
        for (double v : values) {
            buf_.appendChar(',');
            buf_.appendDouble(v);
        }
        buf_.endRow();
    }

    void flush() { buf_.flush(); }

private:
    CsvFileBuf buf_;
};

// A single OutputSink that fans rows out to per-symbol CSV files. Rows route by
// integer locate; the display name arrives once, at onHeader, only to build the
// file path -- so the per-row path never hashes a string.
class CsvMultiplexSink : public OutputSink {
public:
    explicit CsvMultiplexSink(std::string dir) : dir_(std::move(dir)) {}

    void onHeader(std::uint16_t locate, std::string_view symbol,
                  const std::vector<std::string>& columns) override {
        writers_[locate] = std::make_unique<CsvRowWriter>(
            dir_ + "/" + std::string(symbol) + ".csv", columns);
    }

    void onRow(const SampleRow& row) override {
        std::unordered_map<std::uint16_t, std::unique_ptr<CsvRowWriter>>::iterator it =
            writers_.find(row.locate);
        if (it != writers_.end() && it->second->valid()) {
            it->second->writeRow(row.index, row.ts, row.values);
        }
    }

    void flush() override {
        for (std::pair<const std::uint16_t, std::unique_ptr<CsvRowWriter>>& kv : writers_) {
            kv.second->flush();
        }
    }

private:
    std::string dir_;
    std::unordered_map<std::uint16_t, std::unique_ptr<CsvRowWriter>> writers_;
};

}
