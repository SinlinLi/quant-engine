#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace qe {

class SymbolTable {
public:
    uint16_t id(const std::string& name);
    uint16_t find(const std::string& name) const;  // 只读查找，不存在则抛异常
    const std::string& name(uint16_t id) const;
    uint16_t size() const { return static_cast<uint16_t>(names_.size()); }
    bool contains(const std::string& name) const;

private:
    std::unordered_map<std::string, uint16_t> to_id_;
    std::vector<std::string> names_;
};

}  // namespace qe
