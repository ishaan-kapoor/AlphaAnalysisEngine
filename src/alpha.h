#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "trade.h"

namespace hft {

class Instrument;
struct MarketEvent;

class Alpha {
public:
    explicit Alpha(std::size_t dim) : out_(dim, 0.0) {
        out_names_.reserve(dim);
        if (dim == 1) {
            out_names_.emplace_back();  // single channel: bare alpha name, no numeric suffix
        } else {
            for (std::size_t i = 0; i < dim; ++i) {
                out_names_.push_back(std::to_string(i));
            }
        }
    }
    virtual ~Alpha() = default;

    std::size_t dim() const { return out_.size(); }
    const std::vector<double>& output() const { return out_; }
    const std::vector<std::string>& outputNames() const { return out_names_; }

    virtual void onBookUpdate(const MarketEvent&, const Instrument&) {}
    virtual void onTrade(const Trade&, const Instrument&) {}
    virtual void onStatus(const Instrument&) {}

    virtual const char* name() const = 0;
    virtual double value() const = 0;  // primary output value

protected:
    std::vector<double> out_;
    std::vector<std::string> out_names_;
};

}
