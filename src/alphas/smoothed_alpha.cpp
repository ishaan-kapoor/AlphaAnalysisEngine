#include <stdexcept>

#include "alpha.h"
#include "alpha_registry.h"
#include "alphas/imbalance_alpha.h"
#include "instrument.h"

namespace hft {

// Example of a dependent alpha (alpha B that consumes alpha A).
//
// It reads another alpha's output rather than the book directly: an
// exponential moving average of the source's imbalance. Holding the source as a
// concrete ImbalanceAlpha& lets it read the named field source_.imbalance()
// instead of out_[0]. This is also a stateful alpha: the previous EMA lives in
// out_[0] across ticks.
//
// The dependency is wired by injecting a reference to the source alpha at
// construction. Correctness requires the source to run *before* this alpha each
// tick; the engine runs alphas in registration order, so register the source
// first.
class SmoothedImbalanceAlpha : public Alpha {
public:
    explicit SmoothedImbalanceAlpha(const ImbalanceAlpha& source)
        : Alpha(1), source_(source) {
        out_names_ = { "value" };
    }

    void onBookUpdate(const MarketEvent&, const Instrument&) override {
        const double x = source_.imbalance();
        if (!seeded_) {
            // Seed the EMA with the first real sample instead of blending up
            // from 0. imbalance() reports 0 while the book is one-sided, so we
            // wait for a non-zero reading before we treat ourselves as seeded.
            out_[0] = x;
            seeded_ = (x != 0.0);
        } else {
            out_[0] = kLambda * x + (1.0 - kLambda) * out_[0];
            // if we hadn't checked the seeding logic, we would have to apply
            // bias correction: out_[0]/pow(1-kLambda, n)
        }
    }

    double value() const override { return out_[0]; } // the smoothed imbalance

    const char* name() const override { return "smoothed_imbalance"; }

private:
    static constexpr double kLambda = 0.2;
    const ImbalanceAlpha& source_;
    bool seeded_ = false;
};

// smoothed_imbalance: EMA of top_book's imbalance. It needs a reference to
// "top_book" (must be built earlier in the config)
HFT_REGISTER_ALPHA("smoothed_imbalance", [](AlphaContext& c) -> Alpha& {
    ImbalanceAlpha* src = dynamic_cast<ImbalanceAlpha*>(c.resolve("top_book"));
    if (!src)
        throw std::runtime_error(
            "smoothed_imbalance requires a 'top_book' listed before it");
    return c.inst.addAlpha<SmoothedImbalanceAlpha>(*src);
})

}
