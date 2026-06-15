#include <cstddef>
#include <stdexcept>
#include <string>

#include "alpha_registry.h"
#include "instrument.h"

namespace hft {

class FairPrice : public Alpha {
public:
    FairPrice(double decay, std::size_t levels)
        : Alpha(1), decay_(decay), levels_(levels) {}

    void onBookUpdate(const MarketEvent&, const Instrument& inst) override {
        const OrderBook& b = inst.book();
        double num = 0.0;
        double den = 0.0;
        accumulate(b.bids(), num, den);
        accumulate(b.asks(), num, den);
        if (den > 0.0) {  // empty book only: keep the last price, never /0 or snap to 0
            out_[0] = num / den;
        }
    }

    double value() const override { return out_[0]; }

    const char* name() const override { return "fair_price"; }

private:
    template <class Levels>
    void accumulate(const Levels& side, double& num, double& den) const {
        double w = 1.0;
        std::size_t n = 0;
        for (const typename Levels::value_type& level : side) {
            if (n++ == levels_) { break; }
            const double wq = w * static_cast<double>(level.second.qty);
            num += wq * static_cast<double>(level.first); // qty*price
            den += wq;
            w *= decay_;
        }
    }

    double decay_;
    std::size_t levels_;
};

// Depth-weighted fair price. Two construction params per config entry:
//   decay  in (0,1]   per-level exponential weight (smaller = top of book wins)
//   levels >= 1       how many price levels per side to fold in
HFT_REGISTER_ALPHA("fair_price", [](AlphaContext& c) -> Alpha& {
    const double decay = c.params.getDouble("decay", 0.5);
    const int levels = static_cast<int>(c.params.getDouble("levels", 5));
    if (decay <= 0.0 || decay > 1.0) {
        throw std::runtime_error("fair_price: bad decay " + std::to_string(decay) +
                                 " (want 0 < decay <= 1)");
    }
    if (levels < 1) {
        throw std::runtime_error("fair_price: bad levels " + std::to_string(levels) +
                                 " (want >= 1)");
    }
    return c.inst.addAlpha<FairPrice>(decay, static_cast<std::size_t>(levels));
})

}
