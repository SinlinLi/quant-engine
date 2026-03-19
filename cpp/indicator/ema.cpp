#include "indicator/ema.h"
#include <stdexcept>

namespace qe {

EMA::EMA(int period)
    : period_(period > 0 ? period : throw std::invalid_argument("period must be positive")),
      multiplier_(2.0 / (period + 1)) {}

void EMA::update(const Bar& bar) {
    ++count_;
    if (count_ <= period_) {
        // 前 period 根 bar 用 SMA 作为 EMA 初始值
        sum_ += bar.close;
        if (count_ == period_)
            value_ = sum_ / period_;
    } else {
        value_ = (bar.close - value_) * multiplier_ + value_;
    }
}

}  // namespace qe
