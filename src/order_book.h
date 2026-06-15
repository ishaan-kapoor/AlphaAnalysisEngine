#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "market_event.h"

namespace hft {

// L3 / market-by-order book: every order is tracked by id, but each price
// level is an aggregate, not a list of orders. We keep no intra-level queue
// order on purpose since this is not a matching engine, but analysis.
class OrderBook {
public:
    struct LevelAgg {
        Qty          qty = 0;
        std::int32_t count = 0;
    };

    using BidLevels = std::map<Price, LevelAgg, std::greater<Price>>;
    using AskLevels = std::map<Price, LevelAgg, std::less<Price>>;

    // Returns true when the visible book changed, false when nothing visible moved.
    bool apply(const MarketEvent& event) {
        switch (event.type) {
            case EventType::New:
                orders_[event.order_id] =
                    Order{event.side, event.price, event.qty, event.attribution};
                return bump(event.side, event.price, +event.qty, +1);

            case EventType::Cancel:        // partial: shrink the order
            case EventType::ExecVisible:   // a fill also just shrinks it
            case EventType::ExecWithPrice: // exec at a print price: book still shrinks
                return reduce(event, false);

            case EventType::Delete:        // drop the order
                return reduce(event, true);

            case EventType::Replace:       // atomic cancel original + add new
                return replace(event);
            // tape: prints, never on the visible book
            case EventType::Trade:
            case EventType::CrossTrade:
            case EventType::TradingStatus: // per-symbol status: not a book mutation
            // system-wide: the runner never routes these to a book
            case EventType::SystemEvent:
            case EventType::McwbDecline:
            case EventType::McwbStatus:
            // Recognized but not modelled
            case EventType::StockDirectory:
            case EventType::RegSho:
            case EventType::MarketParticipant:
            case EventType::IpoQuoting:
            case EventType::LuldAuctionCollar:
            case EventType::OperationalHalt:
            case EventType::Rpii:
            case EventType::BrokenTrade:
            case EventType::Noii:
            case EventType::Dlcr:
                return false;
        }
        return false;
    }

    bool peekOrder(OrderId id, Side& side, Price& price, Mpid& attribution) const {
        std::unordered_map<OrderId, Order>::const_iterator it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }
        side = it->second.side;
        price = it->second.price;
        attribution = it->second.attribution;
        return true;
    }

    Mpid orderAttribution(OrderId id) const {
        std::unordered_map<OrderId, Order>::const_iterator it = orders_.find(id);
        return it == orders_.end() ? kNoMpid : it->second.attribution;
    }

    bool emptyBid() const { return bids_.empty(); }
    bool emptyAsk() const { return asks_.empty(); }

    // Top of book. Caller must ensure the side is non-empty.
    Price bestBid() const { return bids_.begin()->first; }
    Price bestAsk() const { return asks_.begin()->first; }
    Qty bestBidQty() const { return bids_.begin()->second.qty; }
    Qty bestAskQty() const { return asks_.begin()->second.qty; }

    // Both sides present (a top of book exists); and the self-cross check a
    // correctly reconstructed book must never satisfy.
    bool twoSided() const { return !bids_.empty() && !asks_.empty(); }
    bool crossed() const { return twoSided() && bestBid() >= bestAsk(); }

    const BidLevels& bids() const { return bids_; }
    const AskLevels& asks() const { return asks_; }
    std::size_t liveOrders() const { return orders_.size(); }

private:
    struct Order {
        Side  side{};
        Price price{};
        Qty   qty{};
        Mpid  attribution{};
    };

    bool reduce(const MarketEvent& orderEvent, bool full) {
        std::unordered_map<OrderId, Order>::iterator it = orders_.find(orderEvent.order_id);
        if (it == orders_.end()) {
            throw std::runtime_error(
                "OrderBook::reduce: unknown order " +
                std::to_string(orderEvent.order_id) + " (locate=" +
                std::to_string(orderEvent.locate) + ")");
        }
        Order& order = it->second;
        const Side side = order.side;
        const Price px = order.price;
        const Qty dec = full ? order.qty : std::min(orderEvent.qty, order.qty);
        order.qty -= dec;
        const bool gone = full || order.qty <= 0;
        if (gone) { orders_.erase(it); }
        return bump(side, px, -dec, gone ? -1 : 0);
    }

    bool replace(const MarketEvent& orderEvent) {
        std::unordered_map<OrderId, Order>::iterator it = orders_.find(orderEvent.order_id);
        if (it == orders_.end()) {
            throw std::runtime_error(
                "OrderBook::replace: unknown original order " +
                std::to_string(orderEvent.order_id) + " (locate=" +
                std::to_string(orderEvent.locate) +
                ", new=" + std::to_string(orderEvent.new_order_id) + ")");
        }
        const Side side = it->second.side;
        const Price oldPx = it->second.price;
        const Qty oldQty = it->second.qty;
        const Mpid mpid = it->second.attribution;
        orders_.erase(it);
        bump(side, oldPx, -oldQty, -1);
        orders_[orderEvent.new_order_id] = Order{side, orderEvent.price, orderEvent.qty, mpid};
        return bump(side, orderEvent.price, +orderEvent.qty, +1);
    }

    bool bump(Side s, Price px, Qty dQty, int dCount) {
        return s == Side::Bid ? applyDelta(bids_, px, dQty, dCount)
                              : applyDelta(asks_, px, dQty, dCount);
    }

    template <class Levels>
    static bool applyDelta(Levels& lvls, Price px, Qty dQty, int dCount) {
        LevelAgg& agg = lvls[px];
        agg.qty += dQty;
        agg.count += dCount;
        if (agg.count < 0) { agg.count = 0; }
        if (agg.qty <= 0) { lvls.erase(px); }
        return true;
    }

    std::unordered_map<OrderId, Order> orders_;
    BidLevels bids_;
    AskLevels asks_;
};

}
