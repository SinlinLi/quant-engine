#pragma once

#include <string>
#include "core/strategy.h"
#include "indicator/sma.h"

namespace qe {

class DualMA : public Strategy {
public:
    DualMA(const std::string& symbol, int fast_period, int slow_period);

    void on_init(Context& ctx) override;
    void on_bar(Context& ctx, uint16_t symbol_id, const Bar& bar) override;
    void on_stop(Context& ctx) override;

private:
    std::string symbol_name_;
    int fast_period_;
    int slow_period_;
    uint16_t sid_ = 0;
    SMA* fast_ = nullptr;
    SMA* slow_ = nullptr;
};

}  // namespace qe
