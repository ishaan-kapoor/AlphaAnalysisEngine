#include <stdexcept>
#include <string>

#include "alpha_registry.h"
#include "instrument.h"

namespace hft {

// Values are the config `params.mode`: 0=Mid, 1=Micro, 2=LastTrade.
enum class PriceMode { Mid = 0, Micro = 1, LastTrade = 2 };

class PriceAlpha : public Alpha {
public:
    explicit PriceAlpha(PriceMode mode) : Alpha(1), mode_(mode) {
        out_names_ = { "price" };
    }

    void onBookUpdate(const MarketEvent& /*ev*/, const Instrument& inst) override { recompute(inst); }
    void onTrade(const Trade& /*t*/, const Instrument& inst) override { recompute(inst); }

    double value() const override { return out_[0]; }

    const char* name() const override { return "price"; }

private:
    // Keeps the previous value when the chosen source is momentarily undefined
    // (e.g. an empty side, or no trade yet), so the price never snaps to 0.
    void recompute(const Instrument& inst) {
        if (mode_ == PriceMode::LastTrade) {
            const double p = static_cast<double>(inst.context().lastTradePrice);
            if (p > 0) {
                out_[0] = p;
            }
            return;
        }
        const OrderBook& b = inst.book();
        if (!b.twoSided()) {
            return;
        }
        const double bid = static_cast<double>(b.bestBid());
        const double ask = static_cast<double>(b.bestAsk());
        if (mode_ == PriceMode::Mid) {
            out_[0] = 0.5 * (bid + ask);
        } else { // Micro: size-weighted mid
            const double bq = static_cast<double>(b.bestBidQty());
            const double aq = static_cast<double>(b.bestAskQty());
            const double tot = bq + aq;
            out_[0] = (tot > 0) ? (bid * aq + ask * bq) / tot : 0.5 * (bid + ask);
        }
    }

    PriceMode mode_;
};

// a configurable reference price as a alpha.
// forward-return lead is usually measured against it.
HFT_REGISTER_ALPHA("price", [](AlphaContext& c) -> Alpha& {
    const int m = static_cast<int>(c.params.getDouble("mode", 0));
    if (m < 0 || m > 2)
        throw std::runtime_error("price: bad mode " + std::to_string(m) +
                                 " (want 0=mid, 1=micro, 2=last_trade)");
    return c.inst.addAlpha<PriceAlpha>(static_cast<PriceMode>(m));
})

}
