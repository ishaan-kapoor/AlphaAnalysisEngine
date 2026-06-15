#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "csv_buf.h"
#include "market_event.h"
#include "order_book.h"
#include "types.h"

namespace hft {

// Cell-oriented CSV line writer: callers append typed cells in any order and
// call endRow(); commas are inserted automatically between cells. File
// management, buffering and number formatting live in CsvFileBuf.
class CsvLineBuf {
public:
    explicit CsvLineBuf(const std::string& path) : buf_(path) {}

    bool valid() const { return buf_.valid(); }

    void text(std::string_view s) { sep(); buf_.appendText(s); }
    void integer(std::int64_t v) { sep(); buf_.appendInt(v); }
    void uinteger(std::uint64_t v) { sep(); buf_.appendUInt(v); }
    void blank() { sep(); } // empty cell (keeps the column count fixed)

    void endRow() {
        buf_.endRow();
        atLineStart_ = true;
    }
    void flush() { buf_.flush(); }

private:
    // Emit the comma before every cell except the first on a line.
    void sep() {
        if (!atLineStart_) { buf_.appendChar(','); }
        atLineStart_ = false;
    }

    CsvFileBuf buf_;
    bool atLineStart_ = true;
};

// Decodes feed and writes in a human readable format.
class FeedTraceWriter {
public:
    explicit FeedTraceWriter(const std::string& path) : buf_(path) {
        buf_.text("event_index");
        buf_.text("ts_ns");
        buf_.text("type");
        buf_.text("side");
        buf_.text("order_id");
        buf_.text("new_order_id");
        buf_.text("price");
        buf_.text("qty");
        buf_.text("status");
        buf_.endRow();
    }

    bool valid() const { return buf_.valid(); }

    void write(std::uint64_t index, const MarketEvent& e) {
        buf_.uinteger(index);
        buf_.uinteger(e.ts);
        buf_.text(typeCode(e.type));

        if (e.type == EventType::New || e.type == EventType::Trade) {
            buf_.text(e.side == Side::Bid ? "B" : "A");
        } else {
            buf_.blank();
        }

        if (isOrderEvent(e.type)) {
            buf_.uinteger(e.order_id);
        } else {
            buf_.blank();
        }

        if (e.type == EventType::Replace) {
            buf_.uinteger(e.new_order_id);
        } else {
            buf_.blank();
        }

        if (hasPrice(e.type)) {
            buf_.integer(e.price);
        } else {
            buf_.blank();
        }

        if (hasQty(e.type)) {
            buf_.integer(e.qty);
        } else {
            buf_.blank();
        }

        if (e.type == EventType::TradingStatus) {
            buf_.text(std::string_view(&e.status, 1));
        } else {
            buf_.blank();
        }

        buf_.endRow();
    }

    void flush() { buf_.flush(); }

private:
    static std::string_view typeCode(EventType t) {
        switch (t) {
            case EventType::New:           return "NEW";
            case EventType::Cancel:        return "CANCEL";
            case EventType::Delete:        return "DELETE";
            case EventType::ExecVisible:   return "EXEC";
            case EventType::ExecWithPrice: return "EXECP";
            case EventType::Replace:       return "REPLACE";
            case EventType::Trade:         return "TRADE";
            case EventType::CrossTrade:    return "XTRADE";
            case EventType::TradingStatus: return "STATUS";
            // System-wide events go to feed.csv (SystemFeedWriter), never here;
            // listed so this switch stays exhaustive
            case EventType::SystemEvent:   return "SYSTEM";
            case EventType::McwbDecline:   return "MWCB_DECLINE";
            case EventType::McwbStatus:    return "MWCB_STATUS";
            // Recognized-but-not-modelled
            case EventType::StockDirectory:    return "DIRECTORY";
            case EventType::RegSho:            return "REG_SHO";
            case EventType::MarketParticipant: return "MPID_POS";
            case EventType::IpoQuoting:        return "IPO_QUOTE";
            case EventType::LuldAuctionCollar: return "LULD_COLLAR";
            case EventType::OperationalHalt:   return "OP_HALT";
            case EventType::Rpii:              return "RPII";
            case EventType::BrokenTrade:       return "BROKEN";
            case EventType::Noii:              return "NOII";
            case EventType::Dlcr:              return "DLCR";
        }
        return "?";
    }

    static bool hasPrice(EventType t) {
        return t == EventType::New || t == EventType::ExecWithPrice ||
               t == EventType::Replace || t == EventType::Trade ||
               t == EventType::CrossTrade;
    }

    static bool hasQty(EventType t) {
        return t == EventType::New || t == EventType::Cancel ||
               t == EventType::ExecVisible || t == EventType::ExecWithPrice ||
               t == EventType::Replace || t == EventType::Trade ||
               t == EventType::CrossTrade;
    }

    CsvLineBuf buf_;
};

// Dumps the orderbook for review
class OrderBookWriter {
public:
    OrderBookWriter(const std::string& path, std::size_t depth)
        : buf_(path), depth_(depth) {
        buf_.text("event_index");
        buf_.text("ts_ns");
        for (std::size_t i = 1; i <= depth_; ++i) {
            const std::string a = "ask" + std::to_string(i);
            buf_.text(a + "_px");
            buf_.text(a + "_qty");
            const std::string b = "bid" + std::to_string(i);
            buf_.text(b + "_px");
            buf_.text(b + "_qty");
        }
        buf_.endRow();
    }

    bool valid() const { return buf_.valid(); }

    void write(std::uint64_t index, Ts ts, const OrderBook& book) {
        buf_.uinteger(index);
        buf_.uinteger(ts);
        OrderBook::AskLevels::const_iterator ask_it = book.asks().begin();
        const OrderBook::AskLevels::const_iterator ask_end = book.asks().end();
        OrderBook::BidLevels::const_iterator bid_it = book.bids().begin();
        const OrderBook::BidLevels::const_iterator bid_end = book.bids().end();
        for (std::size_t i = 0; i < depth_; ++i) {
            if (ask_it != ask_end) {
                buf_.integer(ask_it->first);
                buf_.integer(ask_it->second.qty);
                ++ask_it;
            } else {
                buf_.blank();
                buf_.blank();
            }
            if (bid_it != bid_end) {
                buf_.integer(bid_it->first);
                buf_.integer(bid_it->second.qty);
                ++bid_it;
            } else {
                buf_.blank();
                buf_.blank();
            }
        }
        buf_.endRow();
    }

    void flush() { buf_.flush(); }

private:
    CsvLineBuf buf_;
    std::size_t depth_;
};

// Dumps the market-wide (non-instrument) events
class SystemFeedWriter {
public:
    explicit SystemFeedWriter(const std::string& path) : buf_(path) {
        buf_.text("ts_ns");
        buf_.text("type");
        buf_.text("code");
        buf_.text("desc");
        buf_.text("level1_px");
        buf_.text("level2_px");
        buf_.text("level3_px");
        buf_.endRow();
    }

    bool valid() const { return buf_.valid(); }

    void write(const MarketEvent& e) {
        buf_.uinteger(e.ts);
        switch (e.type) {
            case EventType::SystemEvent:
                buf_.text("SYSTEM");
                buf_.text(std::string_view(&e.status, 1));
                buf_.text(systemDesc(e.status));
                buf_.blank();
                buf_.blank();
                buf_.blank();
                break;
            case EventType::McwbDecline:
                buf_.text("MWCB_DECLINE");
                buf_.blank(); // no single-char code
                buf_.text("mwcb_decline_levels");
                // reusing the existing MarketEvent
                buf_.integer(e.price);
                buf_.integer(e.qty);
                buf_.integer(static_cast<std::int64_t>(e.order_id));
                break;
            case EventType::McwbStatus:
                buf_.text("MWCB_STATUS");
                buf_.text(std::string_view(&e.status, 1));
                buf_.text("mwcb_breach");
                buf_.blank();
                buf_.blank();
                buf_.blank();
                break;
            default: // never reached
                buf_.blank();
                buf_.blank();
                buf_.blank();
                buf_.blank();
                buf_.blank();
                buf_.blank();
                break;
        }
        buf_.endRow();
    }

    void flush() { buf_.flush(); }

private:
    static std::string_view systemDesc(char code) {
        switch (code) {
            case 'O': return "start_of_messages";
            case 'S': return "start_of_system_hours";
            case 'Q': return "start_of_market_hours";
            case 'M': return "end_of_market_hours";
            case 'E': return "end_of_system_hours";
            case 'C': return "end_of_messages";
            default:  return "unknown";
        }
    }

    CsvLineBuf buf_;
};

}
