#pragma once

#include <cstdint>

#include "types.h"

namespace hft {

// How a trade printed, so trade-driven alphas can treat them differently.
enum class TradeKind : std::uint8_t {
    Visible, // execution of a displayed resting order (ITCH E/C)
    Hidden,  // non-displayed execution (ITCH P)
    Cross,   // opening/closing/halt auction print (ITCH Q)
};

// For Visible/Hidden, `aggressor` is the taker side (opposite of
// the resting order for an execution); for Cross it is undefined.
struct Trade {
    Price     price{};
    Qty       size{};
    Side      aggressor{};
    TradeKind kind{};
    Ts        ts{};
    Mpid      attribution{}; // MPID of the resting order
};

}
