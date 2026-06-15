#pragma once

#include "instrument.h"

namespace hft {

class ImbalanceAlpha : public Alpha {
public:
    ImbalanceAlpha() : Alpha(3) {
        out_names_ = { "imbalance", "micro_price", "spread"};
    }

    void onBookUpdate(const MarketEvent& /*ev*/, const Instrument& inst) override {
        const OrderBook& b = inst.book();
        if (!b.twoSided()) {
            out_[0] = out_[1] = out_[2] = 0.0;
            return;
        }
        const double bidPx = static_cast<double>(b.bestBid());
        const double askPx = static_cast<double>(b.bestAsk());
        const double bidQ  = static_cast<double>(b.bestBidQty());
        const double askQ  = static_cast<double>(b.bestAskQty());
        const double totQ  = bidQ + askQ;

        const double imbalance = (bidQ - askQ) / totQ;

        out_[0] = imbalance;
        out_[1] = (bidPx * askQ + askPx * bidQ) / totQ; // micro-price
        out_[2] = askPx - bidPx;                        // spread (ticks)
    }

    double value() const override { return out_[0]; }
    double imbalance()  const { return out_[0]; }
    double microPrice() const { return out_[1]; }
    double spread()     const { return out_[2]; }

    const char* name() const override { return "top_book"; }
};

}
