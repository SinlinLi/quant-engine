#pragma once

#include <cstdint>
#include <vector>
#include "core/order.h"
#include "data/bar.h"

namespace qe {

class Portfolio {
public:
    explicit Portfolio(double cash, uint16_t max_symbols = 256);

    const Position& position(uint16_t symbol_id) const;
    double cash() const { return cash_; }
    double equity() const { return equity_; }

    // 成交后更新仓位
    FillEvent fill_order(Order& order, double fill_price, int64_t timestamp_ms,
                         double commission_rate);

    // 每根 bar 更新未实现盈亏和净值
    void update_price(uint16_t symbol_id, double price);
    void recalc_equity();

private:
    double cash_;
    double equity_;
    std::vector<Position> positions_;
    std::vector<double> last_prices_;
};

}  // namespace qe
