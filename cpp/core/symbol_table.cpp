#include "core/symbol_table.h"
#include <stdexcept>

namespace qe {

uint16_t SymbolTable::id(const std::string& name) {
    auto it = to_id_.find(name);
    if (it != to_id_.end())
        return it->second;

    if (names_.size() >= 65535)
        throw std::overflow_error("symbol table full (max 65535)");
    auto new_id = static_cast<uint16_t>(names_.size());
    to_id_[name] = new_id;
    names_.push_back(name);
    return new_id;
}

const std::string& SymbolTable::name(uint16_t id) const {
    if (id >= names_.size())
        throw std::out_of_range("invalid symbol id");
    return names_[id];
}

uint16_t SymbolTable::find(const std::string& name) const {
    auto it = to_id_.find(name);
    if (it == to_id_.end())
        throw std::out_of_range("unknown symbol: " + name);
    return it->second;
}

bool SymbolTable::contains(const std::string& name) const {
    return to_id_.count(name) > 0;
}

}  // namespace qe
