#pragma once
#include "indicator/indicator.h"

namespace qe {

class RSI : public Indicator {
public:
    explicit RSI(int period = 14);
    void update(const Bar& bar) override;
    double value() const override { return value_; }
    bool ready() const override { return count_ > period_; }
    int period() const { return period_; }

private:
    int period_;
    int count_ = 0;
    double prev_close_ = 0.0;
    double avg_gain_ = 0.0;
    double avg_loss_ = 0.0;
    double value_ = 50.0;
};

}  // namespace qe
