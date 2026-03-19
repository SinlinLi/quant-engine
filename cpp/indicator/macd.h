#pragma once
#include "indicator/indicator.h"

namespace qe {

class MACD : public Indicator {
public:
    MACD(int fast_period = 12, int slow_period = 26, int signal_period = 9);
    void update(const Bar& bar) override;
    double value() const override { return macd_line_; }
    bool ready() const override { return count_ >= slow_period_ && signal_count_ >= signal_period_; }

    double signal() const { return signal_line_; }
    double histogram() const { return macd_line_ - signal_line_; }
    int fast_period() const { return fast_period_; }
    int slow_period() const { return slow_period_; }
    int signal_period() const { return signal_period_; }

private:
    int fast_period_;
    int slow_period_;
    int signal_period_;
    double fast_mult_;
    double slow_mult_;
    double signal_mult_;
    double fast_ema_ = 0.0;
    double slow_ema_ = 0.0;
    double signal_line_ = 0.0;
    double macd_line_ = 0.0;
    double fast_sum_ = 0.0;
    double slow_sum_ = 0.0;
    double signal_sum_ = 0.0;
    int count_ = 0;
    int signal_count_ = 0;
};

}  // namespace qe
