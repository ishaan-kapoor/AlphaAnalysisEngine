#include "alpha_registry.h"

#include <stdexcept>
#include <utility>

namespace hft {

AlphaRegistry& AlphaRegistry::instance() {
    static AlphaRegistry registry;
    return registry;
}

void AlphaRegistry::add(std::string name, AlphaFactory factory) {
    factories_[std::move(name)] = std::move(factory);
}

bool AlphaRegistry::known(const std::string& name) const {
    return factories_.find(name) != factories_.end();
}

Alpha& AlphaRegistry::build(const std::string& name, AlphaContext& ctx) const {
    std::unordered_map<std::string, AlphaFactory>::const_iterator it =
        factories_.find(name);
    if (it == factories_.end()) {
        throw std::runtime_error("unknown alpha '" + name + "'");
    }
    return it->second(ctx);
}

std::vector<std::string> AlphaRegistry::names() const {
    std::vector<std::string> out;
    out.reserve(factories_.size());
    for (const std::pair<const std::string, AlphaFactory>& kv : factories_) {
        out.push_back(kv.first);
    }
    return out;
}

}
