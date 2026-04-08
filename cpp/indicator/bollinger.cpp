#include "indicator/bollinger.h"
#include <cmath>
#include <stdexcept>

namespace qe {

// population stddev (除以 N), 金融行业 Bollinger Bands 标准用法
Bollinger::Bollinger(int period, double num_std)
    : period_(period > 0 ? period : throw std::invalid_argument("period must be positive")),
      num_std_(num_std), buf_(period) {}

void Bollinger::update(const Bar& bar) {
    double new_val = bar.close;

    if (buf_.full()) {
        // 窗口已满：滑动 Welford —— 移除最旧值，加入新值
        double old_val = buf_[period_ - 1];  // 即将被覆盖的最旧值
        double old_mean = mean_;
        mean_ += (new_val - old_val) / period_;
        // m2 增量更新：加入新值的贡献，减去旧值的贡献
        m2_ += (new_val - old_mean) * (new_val - mean_)
             - (old_val - old_mean) * (old_val - mean_);
        // 浮点误差可能导致 m2_ 微小负值
        if (m2_ < 0.0) m2_ = 0.0;
    } else {
        // 窗口未满：标准 Welford 递推
        double old_mean = mean_;
        mean_ += (new_val - mean_) / (buf_.count() + 1);
        m2_ += (new_val - old_mean) * (new_val - mean_);
    }

    buf_.push(new_val);

    if (!buf_.full())
        return;

    middle_ = buf_.sum() / period_;
    double variance = m2_ / period_;  // population variance
    double stddev = std::sqrt(variance);

    upper_ = middle_ + num_std_ * stddev;
    lower_ = middle_ - num_std_ * stddev;
}

}  // namespace qe
