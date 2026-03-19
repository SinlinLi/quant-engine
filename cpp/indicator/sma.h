#pragma once

#include "indicator/indicator.h"
#include "indicator/ring_buffer.h"

namespace qe {

class SMA : public Indicator {
public:
    explicit SMA(int period);

    void update(const Bar& bar) override;
    double value() const override { return value_; }
    bool ready() const override { return buf_.full(); }
    int period() const { return period_; }

private:
    int period_;
    RingBuffer buf_;
    double value_ = 0.0;
};

}  // namespace qe
