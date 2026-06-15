#pragma once

#include <cstdint>

#include "types.h"

namespace hft {

using OrderId = std::uint64_t;

// using https://www.nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHspecification.pdf
enum class EventType : std::uint8_t {
    // order lifecycle: these mutate the limit order book
    New,             // 'A'/'F': a new limit order joins the book ('F' also sets `attribution`)
    Cancel,          // 'X': partial cancel: shrink a resting order by `qty`
    Delete,          // 'D': full cancel: remove a resting order entirely
    ExecVisible,     // 'E': resting visible order (partially) filled at its display price
    ExecWithPrice,   // 'C': resting visible order filled at a different print price (`price`)
    Replace,         // 'U': atomic cancel of order_id + add of new_order_id
    // tape: executions that print but never touch the visible book
    Trade,           // 'P': non-displayed (hidden) execution print
    CrossTrade,      // 'Q': opening/closing/halt auction cross print
    // per-symbol trading status
    TradingStatus,   // 'H': trading action / halt-resume (`status`)
    // system-wide: NOT tied to an instrument
    SystemEvent,     // 'S': day-phase marker (`status` = O/S/Q/M/E/C)
    McwbDecline,     // 'V': market-wide circuit-breaker decline levels (3 prices)
    McwbStatus,      // 'W': a circuit-breaker level was breached (`status` = 1/2/3)

    // RECOGNIZED (but not modelled)
    // reference / directory metadata (per-symbol)
    StockDirectory,    // 'R': locate<->symbol map (ItchFeed reads it for naming)
    RegSho,            // 'Y': Reg SHO short-sale price-test restriction
    MarketParticipant, // 'L': market-participant (MPID) position
    IpoQuoting,        // 'K': IPO quoting-period update
    LuldAuctionCollar, // 'J': LULD auction collar (reopen price band)
    OperationalHalt,   // 'h': per-market-center operational halt
    Rpii,              // 'N': retail price-improvement indicator
    // trade admin
    BrokenTrade,       // 'B': a previously printed execution was busted
    // auction / imbalance signal
    Noii,              // 'I': net order imbalance indicator (open/close cross)
    Dlcr,              // 'O': direct listing with capital raise price discovery
};


// Execution of a resting order: reduces the book and counts as a trade.
inline constexpr bool isExecution(EventType t) {
    return t == EventType::ExecVisible || t == EventType::ExecWithPrice;
}

// Order-lifecycle events that mutate the limit order book
inline constexpr bool isOrderEvent(EventType t) {
    return t == EventType::New || t == EventType::Cancel ||
           t == EventType::Delete || t == EventType::Replace || isExecution(t);
}
inline constexpr bool affectsBook(EventType t) { return isOrderEvent(t); }

// Tape-only trade print that does NOT touch the book
inline constexpr bool isTradePrint(EventType t) {
    return t == EventType::Trade || t == EventType::CrossTrade;
}

// Any event that results in an executed trade
inline constexpr bool isTrade(EventType t) {
    return isExecution(t) || isTradePrint(t);
}

inline constexpr bool isAuction(EventType t) { return t == EventType::CrossTrade; }

inline constexpr bool isStatus(EventType t) { return t == EventType::TradingStatus; }

// Market-wide events not tied to any instrument
inline constexpr bool isSystemWide(EventType t) {
    return t == EventType::SystemEvent || t == EventType::McwbDecline ||
           t == EventType::McwbStatus;
}

struct MarketEvent {
    EventType     type{};
    Side          side{};         // New / Trade: order or trade side
    OrderId       order_id{};     // order actions; original ref for Replace; McwbDecline: level-3 price
    OrderId       new_order_id{}; // Replace: id of the replacement order
    Price         price{};        // order price; print price (ExecWithPrice/Trade/CrossTrade); McwbDecline: level-1 price
    Qty           qty{};          // shares affected by THIS action / traded; McwbDecline: level-2 price
    Ts            ts{};
    std::uint16_t locate{};       // per-symbol routing key (ITCH stock-locate; 0 for system-wide)
    char          status{};       // TradingStatus state ('H','T',...); SystemEvent day-phase (O/S/Q/M/E/C); McwbStatus breached level (1/2/3)
    Mpid          attribution{};  // New from an 'F' add: the order's MPID; 0 ('A' / no attribution) for everything else
};

}
