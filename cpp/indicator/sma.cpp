#include "indicator/sma.h"
#include <stdexcept>

namespace qe {

SMA::SMA(int period)
    : period_(period > 0 ? period : throw std::invalid_argument("period must be positive")),
      buf_(period) {}

void SMA::update(const Bar& bar) {
    buf_.push(bar.close);
    if (buf_.full())
        value_ = buf_.sum() / period_;
}

}  // namespace qe
