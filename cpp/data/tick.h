#pragma once

#include <cstdint>

namespace qe {

struct Trade {
    int64_t timestamp_ms = 0;
    double price = 0.0;
    double quantity = 0.0;
    bool is_buyer_maker = false;
};

struct DepthLevel {
    double price;
    double quantity;
};

}  // namespace qe
