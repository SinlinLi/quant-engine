#pragma once

#include <cstdint>

namespace qe {

struct Trade {
    int64_t timestamp_ms;
    double price;
    double quantity;
    bool is_buyer_maker;
};

struct DepthLevel {
    double price;
    double quantity;
};

}  // namespace qe
