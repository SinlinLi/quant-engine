#pragma once
#include "indicator/indicator.h"
#include "indicator/ring_buffer.h"

namespace qe {

class Bollinger : public Indicator {
public:
    explicit Bollinger(int period = 20, double num_std = 2.0);
    void update(const Bar& bar) override;
    double value() const override { return middle_; }
    bool ready() const override { return buf_.full(); }

    double upper() const { return upper_; }
    double lower() const { return lower_; }
    double bandwidth() const { return middle_ > 1e-12 ? (upper_ - lower_) / middle_ : 0.0; }
    int period() const { return period_; }

private:
    int period_;
    double num_std_;
    RingBuffer buf_;
    double middle_ = 0.0;
    double upper_ = 0.0;
    double lower_ = 0.0;
};

}  // namespace qe
