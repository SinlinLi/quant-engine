#pragma once

#include <string>
#include "core/strategy.h"
#include "indicator/macd.h"

namespace qe {

class MACDCross : public Strategy {
public:
    MACDCross(const std::string& symbol, int fast = 12, int slow = 26, int signal = 9);

    void on_init(Context& ctx) override;
    void on_bar(Context& ctx, uint16_t symbol_id, const Bar& bar) override;
    void on_stop(Context& ctx) override;

private:
    std::string symbol_name_;
    int fast_, slow_, signal_;
    uint16_t sid_ = 0;
    MACD* macd_ = nullptr;
    double prev_hist_ = 0.0;
};

}  // namespace qe
