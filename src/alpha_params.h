#pragma once

#include <string>
#include <unordered_map>

namespace hft {

class AlphaParams {
public:
    bool has(const std::string& key) const {
        return values_.find(key) != values_.end();
    }

    double getDouble(const std::string& key, double dflt) const {
        std::unordered_map<std::string, double>::const_iterator it =
            values_.find(key);
        return it == values_.end() ? dflt : it->second;
    }

    void set(const std::string& key, double value) { values_[key] = value; }

private:
    std::unordered_map<std::string, double> values_;
};

}
