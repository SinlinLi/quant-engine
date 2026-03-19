#pragma once

#include "indicator/indicator.h"

namespace qe {

class EMA : public Indicator {
public:
    explicit EMA(int period);

    void update(const Bar& bar) override;
    double value() const override { return value_; }
    bool ready() const override { return count_ >= period_; }
    int period() const { return period_; }

private:
    int period_;
    double multiplier_;
    double value_ = 0.0;
    double sum_ = 0.0;
    int count_ = 0;
};

}  // namespace qe
