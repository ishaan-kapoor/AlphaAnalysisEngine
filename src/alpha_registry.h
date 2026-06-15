#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "alpha_params.h"

namespace hft {

class Alpha;
class Instrument;

struct AlphaContext {
    Instrument& inst;
    std::function<Alpha*(const std::string&)> resolve;
    AlphaParams params{};
};

using AlphaFactory = std::function<Alpha&(AlphaContext&)>;

class AlphaRegistry {
public:
    static AlphaRegistry& instance();

    void add(std::string name, AlphaFactory factory);
    bool known(const std::string& name) const;
    Alpha& build(const std::string& name, AlphaContext& ctx) const;

    std::vector<std::string> names() const; // for diagnostics

private:
    std::unordered_map<std::string, AlphaFactory> factories_;
};

}

#define HFT_DETAIL_CONCAT_(a, b) a##b
#define HFT_DETAIL_CONCAT(a, b) HFT_DETAIL_CONCAT_(a, b)
#define HFT_REGISTER_ALPHA(NAME, ...)                                         \
    namespace {                                                               \
    const bool HFT_DETAIL_CONCAT(hft_alpha_reg_, __COUNTER__) =               \
        (::hft::AlphaRegistry::instance().add((NAME), __VA_ARGS__), true);    \
    }
#define HFT_REGISTER_ALPHA_SIMPLE(NAME, TYPE)                                 \
    HFT_REGISTER_ALPHA(NAME, [](::hft::AlphaContext& c) -> ::hft::Alpha& {    \
        return c.inst.addAlpha<TYPE>();                                       \
    })
