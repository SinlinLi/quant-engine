#include "indicator/bollinger.h"
#include <cmath>
#include <stdexcept>

namespace qe {

// population stddev (除以 N), 金融行业 Bollinger Bands 标准用法
Bollinger::Bollinger(int period, double num_std)
    : period_(period > 0 ? period : throw std::invalid_argument("period must be positive")),
      num_std_(num_std), buf_(period) {}

void Bollinger::update(const Bar& bar) {
    buf_.push(bar.close);
    if (!buf_.full())
        return;

    middle_ = buf_.sum() / period_;

    // 标准差：遍历 buffer 一次
    double sq_sum = 0.0;
    for (int i = 0; i < period_; ++i) {
        double diff = buf_[i] - middle_;
        sq_sum += diff * diff;
    }
    double stddev = std::sqrt(sq_sum / period_);

    upper_ = middle_ + num_std_ * stddev;
    lower_ = middle_ - num_std_ * stddev;
}

}  // namespace qe
