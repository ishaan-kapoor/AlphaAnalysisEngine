#pragma once

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace hft {

// Buffered, allocation-free CSV byte sink shared by the CSV writers.
//
// Fields are written verbatim, with no RFC-4180 quoting, so every
// appended value must be free of commas, quotes and newlines.
class CsvFileBuf {
public:
    explicit CsvFileBuf(const std::string& path) {
        f_ = std::fopen(path.c_str(), "wb");
        if (f_) { buf_.reserve(kFlush + 512); }
    }
    ~CsvFileBuf() {
        flush();
        if (f_) { std::fclose(f_); }
    }
    CsvFileBuf(const CsvFileBuf&) = delete;
    CsvFileBuf& operator=(const CsvFileBuf&) = delete;

    bool valid() const { return f_ != nullptr; }

    void appendInt(std::int64_t v) {
        char tmp[24];
        const std::to_chars_result r = std::to_chars(tmp, tmp + sizeof(tmp), v);
        buf_.insert(buf_.end(), tmp, r.ptr);
    }
    void appendUInt(std::uint64_t v) {
        char tmp[24];
        const std::to_chars_result r = std::to_chars(tmp, tmp + sizeof(tmp), v);
        buf_.insert(buf_.end(), tmp, r.ptr);
    }
    void appendDouble(double v) {
        char tmp[32];
        const std::to_chars_result r = std::to_chars(tmp, tmp + sizeof(tmp), v);
        buf_.insert(buf_.end(), tmp, r.ptr);
    }
    void appendText(std::string_view s) {
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    void appendChar(char c) { buf_.push_back(c); }

    // Flushing to keep rows from being split across fwrite
    void endRow() {
        buf_.push_back('\n');
        if (buf_.size() >= kFlush) { flush(); }
    }

    void flush() {
        if (f_ && !buf_.empty()) {
            std::fwrite(buf_.data(), 1, buf_.size(), f_);
            buf_.clear();
        }
    }

private:
    static constexpr std::size_t kFlush = 1u << 16; // 64 KiB

    std::FILE* f_ = nullptr;
    std::vector<char> buf_;
};

}
