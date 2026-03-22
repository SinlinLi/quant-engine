#include "core/portfolio.h"
#include <cassert>
#include <cmath>
#include <stdexcept>

namespace qe {

Portfolio::Portfolio(double cash, uint16_t max_symbols)
    : cash_(cash), equity_(cash),
      positions_(max_symbols), last_prices_(max_symbols, 0.0),
      buy_commissions_(max_symbols, 0.0) {}

const Position& Portfolio::position(uint16_t symbol_id) const {
    assert(symbol_id < positions_.size());
    return positions_[symbol_id];
}

FillEvent Portfolio::fill_order(Order& order, double fill_price,
                                int64_t timestamp_ms, double commission_rate) {
    assert(order.symbol_id < positions_.size());
    auto& pos = positions_[order.symbol_id];
    double cost = fill_price * order.quantity;
    double commission = cost * commission_rate;
    double pnl = 0.0;

    double buy_side_commission = 0.0;  // 分摊到本次卖出的买入佣金

    if (order.side == Side::BUY) {
        // 更新均价
        double old_value = pos.avg_entry_price * pos.quantity;
        pos.quantity += order.quantity;
        if (pos.quantity > 0)
            pos.avg_entry_price = (old_value + cost) / pos.quantity;
        cash_ -= cost + commission;
        buy_commissions_[order.symbol_id] += commission;
    } else {
        // 卖出: 计算已实现盈亏（不支持做空，卖出量不得超过持仓）
        if (order.quantity > pos.quantity + 1e-12)
            throw std::logic_error("sell quantity exceeds position");
        pnl = (fill_price - pos.avg_entry_price) * order.quantity;
        // 按卖出比例分摊买入佣金
        double sell_ratio = (pos.quantity > 1e-12) ? order.quantity / pos.quantity : 1.0;
        buy_side_commission = buy_commissions_[order.symbol_id] * sell_ratio;
        buy_commissions_[order.symbol_id] -= buy_side_commission;
        pos.quantity -= order.quantity;
        pos.realized_pnl += pnl;
        cash_ += cost - commission;
        if (std::abs(pos.quantity) < 1e-12) {
            pos.quantity = 0.0;
            pos.avg_entry_price = 0.0;
            buy_commissions_[order.symbol_id] = 0.0;
        }
    }

    order.filled_quantity = order.quantity;
    order.commission = commission;
    order.status = OrderStatus::FILLED;
    order.filled_at = timestamp_ms;

    last_prices_[order.symbol_id] = fill_price;

    return FillEvent{
        order.id, order.symbol_id, order.side,
        fill_price, order.quantity, commission, buy_side_commission, pnl, timestamp_ms
    };
}

void Portfolio::update_price(uint16_t symbol_id, double price) {
    assert(symbol_id < positions_.size());
    last_prices_[symbol_id] = price;
    auto& pos = positions_[symbol_id];
    if (pos.quantity > 0)
        pos.unrealized_pnl = (price - pos.avg_entry_price) * pos.quantity;
    else
        pos.unrealized_pnl = 0.0;
}

void Portfolio::recalc_equity() {
    equity_ = cash_;
    for (uint16_t i = 0; i < positions_.size(); ++i) {
        if (positions_[i].quantity > 0)
            equity_ += positions_[i].quantity * last_prices_[i];
    }
}

}  // namespace qe
