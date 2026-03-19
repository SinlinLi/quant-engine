#include "indicator/macd.h"
#include <stdexcept>

namespace qe {

MACD::MACD(int fast_period, int slow_period, int signal_period)
    : fast_period_(fast_period > 0 ? fast_period : throw std::invalid_argument("fast_period must be positive")),
      slow_period_(slow_period > 0 ? slow_period : throw std::invalid_argument("slow_period must be positive")),
      signal_period_(signal_period > 0 ? signal_period : throw std::invalid_argument("signal_period must be positive")),
      fast_mult_(2.0 / (fast_period + 1)),
      slow_mult_(2.0 / (slow_period + 1)),
      signal_mult_(2.0 / (signal_period + 1)) {}

void MACD::update(const Bar& bar) {
    ++count_;

    // Fast EMA
    if (count_ <= fast_period_) {
        fast_sum_ += bar.close;
        if (count_ == fast_period_)
            fast_ema_ = fast_sum_ / fast_period_;
    } else {
        fast_ema_ = (bar.close - fast_ema_) * fast_mult_ + fast_ema_;
    }

    // Slow EMA
    if (count_ <= slow_period_) {
        slow_sum_ += bar.close;
        if (count_ == slow_period_)
            slow_ema_ = slow_sum_ / slow_period_;
    } else {
        slow_ema_ = (bar.close - slow_ema_) * slow_mult_ + slow_ema_;
    }

    // MACD line = fast - slow (only meaningful when slow EMA is initialized)
    if (count_ < slow_period_)
        return;

    macd_line_ = fast_ema_ - slow_ema_;

    // Signal line (EMA of MACD line)
    ++signal_count_;
    if (signal_count_ <= signal_period_) {
        signal_sum_ += macd_line_;
        if (signal_count_ == signal_period_)
            signal_line_ = signal_sum_ / signal_period_;
    } else {
        signal_line_ = (macd_line_ - signal_line_) * signal_mult_ + signal_line_;
    }
}

}  // namespace qe
