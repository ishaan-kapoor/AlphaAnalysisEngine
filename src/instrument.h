#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "alpha.h"
#include "market_event.h"
#include "order_book.h"
#include "trade.h"

namespace hft {

// Derived, non-book market state that alphas often want alongside the book:
// The Instrument maintains it so alphas don't each re-derive it.
struct MarketContext {
    Price lastTradePrice = 0;
    Qty   lastTradeSize  = 0;
    Side  lastAggressor{};
    Qty   buyVolume  = 0;
    Qty   sellVolume = 0;
    bool  halted = false;
    char  tradingState = 0;
    Mpid  lastTradeAttribution = 0;

    void applyTrade(const Trade& t) {
        lastTradePrice = t.price;
        lastTradeSize  = t.size;
        lastAggressor  = t.aggressor;
        lastTradeAttribution = t.attribution;
        if (t.kind != TradeKind::Cross) {
            if (t.aggressor == Side::Bid) { buyVolume += t.size; }
            else { sellVolume += t.size; }
        }
    }
    void applyStatus(char s) {
        tradingState = s;
        halted = (s == 'H');
    }
};

class Instrument {
public:
    explicit Instrument(std::string symbol) : symbol_(std::move(symbol)) {}

    template <class A, class... Args>
    A& addAlpha(Args&&... args) {
        std::unique_ptr<A> p = std::make_unique<A>(std::forward<Args>(args)...);
        A& ref = *p;
        alphas_.push_back(std::move(p));
        return ref;
    }

    // Routes a market event to alphas
    // Returns true iff at least one alpha hook fired,
    // so callers can sample on alpha-updating events.
    bool onEvent(const MarketEvent& event) {
        if (isOrderEvent(event.type)) {
            // An execution is both a book reduction AND a trade: capture the
            // trade before the book mutates, then dispatch after.
            Trade trade;
            bool haveTrade = false;
            if (isExecution(event.type)) {
                Side restingSide;
                Price restingPrice;
                Mpid restingAttribution;
                if (book_.peekOrder(event.order_id, restingSide, restingPrice, restingAttribution)) {
                    trade.price = (event.type == EventType::ExecWithPrice) ? event.price : restingPrice;
                    trade.size = event.qty;
                    trade.aggressor = opposite(restingSide);
                    trade.kind = TradeKind::Visible;
                    trade.ts = event.ts;
                    trade.attribution = restingAttribution;
                    haveTrade = true;
                }
            }
            const bool changed = book_.apply(event);
            bool ran = false;
            if (changed) {
                for (std::unique_ptr<Alpha>& a : alphas_) { a->onBookUpdate(event, *this); }
                ran = true;
            }
            if (haveTrade) {
                ctx_.applyTrade(trade);
                for (std::unique_ptr<Alpha>& a : alphas_) { a->onTrade(trade, *this); }
                ran = true;
            }
            return ran;
        }

        else if (isTradePrint(event.type)) {
            Trade trade;
            trade.price = event.price;
            trade.size = event.qty;
            trade.aggressor = event.side;
            trade.kind = isAuction(event.type) ? TradeKind::Cross : TradeKind::Hidden;
            trade.ts = event.ts;
            ctx_.applyTrade(trade);
            for (std::unique_ptr<Alpha>& a : alphas_) { a->onTrade(trade, *this); }
            return true;
        }

        else if (isStatus(event.type)) {
            ctx_.applyStatus(event.status);
            for (std::unique_ptr<Alpha>& a : alphas_) { a->onStatus(*this); }
            return true;
        }
        return false;
    }

    const std::string& symbol() const { return symbol_; }
    const OrderBook& book() const { return book_; }
    const MarketContext& context() const { return ctx_; }

private:
    std::string symbol_;
    OrderBook book_;
    MarketContext ctx_;
    std::vector<std::unique_ptr<Alpha>> alphas_;
};

}
