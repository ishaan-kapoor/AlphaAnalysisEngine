#pragma once

#include <cstdint>

namespace hft {

// Prices are integer ticks, never floating point: exact comparisons, no
// rounding error, and a tick is the smallest increment the venue allows.
using Price = std::int64_t;

// Quantity in whatever unit the venue uses (shares / contracts / lots).
using Qty = std::int64_t;

// Nanoseconds since midnight.
using Ts = std::uint64_t;

enum class Side : std::uint8_t { Bid, Ask };

inline Side opposite(Side s) { return s == Side::Bid ? Side::Ask : Side::Bid; }

// A NASDAQ market-participant id (MPID): the 4-char Attribution carried only by
// ITCH 'F' adds, packed into one 32-bit word. 0 means "no attribution" -- ITCH
// 'A' adds and every non-add event default to it, and a real MPID is 4 printable
// ASCII chars so it is never 0. One word keeps a resting Order small and makes
// "is this order attributed?" a single integer compare.
using Mpid = std::uint32_t;
inline constexpr Mpid kNoMpid = 0;

}
