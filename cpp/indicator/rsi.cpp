#include "indicator/rsi.h"
#include <cmath>
#include <stdexcept>

namespace qe {

RSI::RSI(int period)
    : period_(period > 0 ? period : throw std::invalid_argument("period must be positive")) {}

void RSI::update(const Bar& bar) {
    ++count_;

    if (count_ == 1) {
        prev_close_ = bar.close;
        return;
    }

    double change = bar.close - prev_close_;
    double gain = change > 0 ? change : 0.0;
    double loss = change < 0 ? -change : 0.0;
    prev_close_ = bar.close;

    if (count_ <= period_ + 1) {
        // 累积阶段：收集前 period 个变化值
        avg_gain_ += gain;
        avg_loss_ += loss;

        if (count_ == period_ + 1) {
            avg_gain_ /= period_;
            avg_loss_ /= period_;
            if (avg_loss_ > 1e-12)
                value_ = 100.0 - 100.0 / (1.0 + avg_gain_ / avg_loss_);
            else
                value_ = 100.0;
        }
    } else {
        // Wilder 平滑
        avg_gain_ = (avg_gain_ * (period_ - 1) + gain) / period_;
        avg_loss_ = (avg_loss_ * (period_ - 1) + loss) / period_;

        if (avg_loss_ > 1e-12)
            value_ = 100.0 - 100.0 / (1.0 + avg_gain_ / avg_loss_);
        else
            value_ = 100.0;
    }
}

}  // namespace qe
