#pragma once

#include <cstdint>

namespace qe {

struct Bar {
    int64_t timestamp_ms;
    double open;
    double high;
    double low;
    double close;
    double volume;
    double quote_volume;
};

}  // namespace qe
