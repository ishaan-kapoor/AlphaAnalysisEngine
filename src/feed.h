#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <endian.h>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "market_event.h"

namespace hft {

class Feed {
public:
    virtual ~Feed() = default;

    // True while the source is open / readable.
    virtual bool valid() const = 0;

    // Fills `e` with the next event; returns false at end of stream.
    virtual bool next(MarketEvent& e) = 0;

    // Human-readable symbol for a per-symbol routing key (locate).
    virtual std::string symbolOf(std::uint16_t locate) const = 0;
};

// Streaming reader for raw NASDAQ ITCH 5.0 binary data
//
// Replay must start at the top of the stream: 'R' announces the locate, and
// every exec/cancel/delete/replace references an order id we must already hold.
//
// The parsing logic of ItchFeed is heavily inspired by
// https://github.com/bbalouki/itchcpp and https://github.com/martinobdl/ITCH
// The Feed/MarketEvent mapping and the locate watchlist are added on top.

class ItchFeed : public Feed {
public:
    explicit ItchFeed(const std::string& path, std::vector<std::string> symbols = {})
        : watch_all_(symbols.empty()) {
        for (std::string& s : symbols) { requested_.insert(pad8(s)); }
        file_.open(path, std::ios::binary);
    }

    bool valid() const override { return static_cast<bool>(file_); }

    std::string symbolOf(std::uint16_t locate) const override {
        std::unordered_map<std::uint16_t, std::string>::const_iterator it =
            names_.find(locate);
        if (it != names_.end()) { return it->second; }
        return "loc:" + std::to_string(locate);
    }

    // Fills event param with the next event for a watched symbol OR a
    // system-wide event. Returns false at end of stream (or on a truncated
    // trailing message).
    bool next(MarketEvent& event) override {
        unsigned char len_buf[2];
        while (readN(len_buf, 2)) {
            const std::size_t len =
                (static_cast<std::size_t>(len_buf[0]) << 8) | len_buf[1];
            if (len > sizeof(msg_)) {
                file_.ignore(static_cast<std::streamsize>(len));
                continue;
            }
            if (len == 0) { continue; }
            if (!readN(msg_, len)) { return false; } // truncated tail
            if (translate(len, event)) { return true; }
        }
        return false;
    }

private:
    static std::string pad8(std::string s) {
        s.resize(8, ' ');
        return s;
    }
    static std::string trim(const char* p, std::size_t n) {
        std::string s(p, n);
        while (!s.empty() && s.back() == ' ') { s.pop_back(); }
        return s;
    }

    bool readN(unsigned char* dst, std::size_t n) {
        file_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
        return file_.gcount() == static_cast<std::streamsize>(n);
    }

    static std::uint16_t rdU16(const unsigned char* p) {
        std::uint16_t v;
        std::memcpy(&v, p, sizeof(v));
        return be16toh(v);
    }
    static std::uint32_t rdU32(const unsigned char* p) {
        std::uint32_t v;
        std::memcpy(&v, p, sizeof(v));
        return be32toh(v);
    }
    static std::uint64_t rdU64(const unsigned char* p) {
        std::uint64_t v;
        std::memcpy(&v, p, sizeof(v));
        return be64toh(v);
    }
    static Ts rdTs6(const unsigned char* p) { // 6-byte ns-since-midnight
        Ts v = 0;
        for (int i = 0; i < 6; ++i) { v = (v << 8) | p[i]; }
        return v;
    }
    // 4-char MPID kept as raw bytes, so 0 == kNoMpid and it round-trips back.
    static Mpid packMpid(const unsigned char* p) {
        Mpid v;
        std::memcpy(&v, p, sizeof(v));
        return v;
    }

    // Maps one raw message to a MarketEvent. Returns true if it produced an
    // event: a watched symbol's order/trade/status, or a system-wide 'S'/'V'/'W'
    // (emitted regardless of the watchlist). 'R' rows only register
    // locate<->symbol; unwatched symbols and unmodelled types return false.
    bool translate(std::size_t len, MarketEvent& e) {
        if (len < 11) { return false; } // smaller than the common header
        const char type = static_cast<char>(msg_[0]);
        const std::uint16_t locate = rdU16(msg_ + 1);

        if (type == 'R') { // Stock Directory (EventType::StockDirectory): learn
                           // the symbol behind this locate; never emitted as an event
            if (len >= 19) {
                names_[locate] = trim(reinterpret_cast<const char*>(msg_ + 11), 8);
                if (watch_all_ ||
                    requested_.count(std::string(
                        reinterpret_cast<const char*>(msg_ + 11), 8))) {
                    watch_.insert(locate);
                }
            }
            return false;
        }

        // System-wide messages belong to no instrument (stock locate is 0), so
        // they are emitted BEFORE the per-symbol watch filter for the global
        // feed.csv dump. They never touch a book or an alpha (see isSystemWide).
        if (type == 'S' || type == 'V' || type == 'W') {
            e = MarketEvent{};
            e.ts = rdTs6(msg_ + 5);
            e.locate = locate;
            if (type == 'S') {        // System Event (len 12): day-phase code
                if (len < 12) { return false; }
                e.type = EventType::SystemEvent;
                e.status = static_cast<char>(msg_[11]);
            } else if (type == 'V') { // MWCB Decline Level (len 35): 3 prices
                if (len < 35) { return false; }
                e.type = EventType::McwbDecline;
                e.price    = static_cast<Price>(rdU64(msg_ + 11)); // level 1
                e.qty      = static_cast<Qty>(rdU64(msg_ + 19));   // level 2
                e.order_id = rdU64(msg_ + 27);                     // level 3
            } else {                  // 'W' MWCB Status (len 12): breached level
                if (len < 12) { return false; }
                e.type = EventType::McwbStatus;
                e.status = static_cast<char>(msg_[11]);
            }
            return true;
        }
        if (!watch_all_ && watch_.find(locate) == watch_.end()) { return false; }

        e.locate = locate;
        e.order_id = 0;
        e.new_order_id = 0;
        e.side = Side::Bid;  // overwritten where it matters; resolved by OrderBook otherwise
        e.price = 0;
        e.qty = 0;
        e.status = 0;
        e.attribution = kNoMpid; // only an 'F' add overrides this below
        e.ts = rdTs6(msg_ + 5);

        switch (type) {
            case 'A':                 // Add Order, no MPID attribution
            case 'F':                 // Add Order with MPID attribution: same first 36 B + 4 B MPID
                if (len < 36) { return false; }
                e.type = EventType::New;
                e.order_id = rdU64(msg_ + 11);
                e.side = (msg_[19] == 'B') ? Side::Bid : Side::Ask;
                e.qty = rdU32(msg_ + 20);
                e.price = static_cast<Price>(rdU32(msg_ + 32));
                // 'F' carries the 4-char Attribution at offset 36; 'A' has no such
                // field, so its attribution stays kNoMpid (set in the init above).
                if (type == 'F' && len >= 40) { e.attribution = packMpid(msg_ + 36); }
                return true;

            case 'E':                 // Order Executed (at the order's display price)
                if (len < 23) {
                    return false;
                }
                e.type = EventType::ExecVisible;
                e.order_id = rdU64(msg_ + 11);
                e.qty = rdU32(msg_ + 19);
                return true;

            case 'C':                 // Order Executed With Price (print price differs)
                if (len < 36) {
                    return false;
                }
                e.type = EventType::ExecWithPrice;
                e.order_id = rdU64(msg_ + 11);
                e.qty = rdU32(msg_ + 19);                       // book reduces by this
                e.price = static_cast<Price>(rdU32(msg_ + 32)); // trade print price
                return true;

            case 'X':                 // Order Cancel (partial)
                if (len < 23) {
                    return false;
                }
                e.type = EventType::Cancel;
                e.order_id = rdU64(msg_ + 11);
                e.qty = rdU32(msg_ + 19);
                return true;

            case 'D':                 // Order Delete (full)
                if (len < 19) {
                    return false;
                }
                e.type = EventType::Delete;
                e.order_id = rdU64(msg_ + 11);
                return true;

            case 'U':                 // Order Replace (cancel original + add new)
                if (len < 35) {
                    return false;
                }
                e.type = EventType::Replace;
                e.order_id = rdU64(msg_ + 11);     // original ref
                e.new_order_id = rdU64(msg_ + 19); // replacement ref
                e.qty = rdU32(msg_ + 27);
                e.price = static_cast<Price>(rdU32(msg_ + 31));
                return true;

            case 'P':                 // Trade (non-cross): a non-displayed execution
                if (len < 36) {
                    return false;
                }
                e.type = EventType::Trade;
                e.side = (msg_[19] == 'B') ? Side::Bid : Side::Ask;
                e.qty = rdU32(msg_ + 20);
                e.price = static_cast<Price>(rdU32(msg_ + 32));
                return true;

            case 'Q':                 // Cross Trade: opening/closing/halt auction print
                if (len < 31) {
                    return false;
                }
                e.type = EventType::CrossTrade;
                e.qty = static_cast<Qty>(rdU64(msg_ + 11)); // cross shares (8 bytes here)
                e.price = static_cast<Price>(rdU32(msg_ + 27));
                return true;

            case 'H':                 // Stock Trading Action (halt / resume / quoting)
                if (len < 20) {
                    return false;
                }
                e.type = EventType::TradingStatus;
                e.status = static_cast<char>(msg_[19]); // trading state code
                return true;

            // ---- recognized but not decoded ---------------------------------
            // Remaining ITCH 5.0 types: identified but not parsed or emitted.
            // To handle one, decode its fields above and return true (target
            // EventType noted per line).
            case 'Y':  // Reg SHO short-sale restriction      -> EventType::RegSho
            case 'L':  // Market Participant Position          -> EventType::MarketParticipant
            case 'K':  // IPO Quoting Period Update            -> EventType::IpoQuoting
            case 'J':  // LULD Auction Collar                  -> EventType::LuldAuctionCollar
            case 'h':  // Operational Halt                     -> EventType::OperationalHalt
            case 'N':  // Retail Price Improvement Indicator   -> EventType::Rpii
            case 'B':  // Broken Trade                         -> EventType::BrokenTrade
            case 'I':  // Net Order Imbalance Indicator        -> EventType::Noii
            case 'O':  // Direct Listing w/ Capital Raise      -> EventType::Dlcr
                return false;

            default:
                // Unknown type (corrupt/desynced frame); length framing
                // realigns the next read, so just skip it.
                return false;
        }
    }

    std::ifstream file_;
    bool watch_all_ = false;
    std::unordered_set<std::string> requested_;
    std::unordered_set<std::uint16_t> watch_;
    std::unordered_map<std::uint16_t, std::string> names_; // locate -> symbol
    unsigned char msg_[64]{};
};

}
