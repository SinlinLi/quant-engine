#include "core/sim_broker.h"
#include <algorithm>

namespace qe {

SimBroker::SimBroker(const SimBrokerConfig& config)
    : config_(config), portfolio_(config.cash) {}

uint64_t SimBroker::submit_order(Order order) {
    if (order.quantity <= 0)
        return 0;  // 无效订单，拒绝提交
    order.id = next_order_id_++;
    order.status = OrderStatus::PENDING;
    pending_orders_.push_back(order);
    return order.id;
}

bool SimBroker::cancel_order(uint64_t order_id) {
    auto it = std::find_if(pending_orders_.begin(), pending_orders_.end(),
        [order_id](const Order& o) { return o.id == order_id; });
    if (it != pending_orders_.end()) {
        it->status = OrderStatus::CANCELLED;
        pending_orders_.erase(it);
        return true;
    }
    return false;
}

const Position& SimBroker::position(uint16_t symbol_id) const {
    return portfolio_.position(symbol_id);
}

double SimBroker::equity() const {
    return portfolio_.equity();
}

double SimBroker::available_cash() const {
    return portfolio_.cash();
}

void SimBroker::on_bar(uint16_t symbol_id, const Bar& bar) {
    portfolio_.update_price(symbol_id, bar.close);

    // 遍历 pending orders，尝试成交
    auto it = pending_orders_.begin();
    while (it != pending_orders_.end()) {
        if (it->symbol_id != symbol_id) {
            ++it;
            continue;
        }
        try_fill(*it, bar);
        if (it->status == OrderStatus::FILLED || it->status == OrderStatus::CANCELLED) {
            Order completed = *it;
            it = pending_orders_.erase(it);
            if (order_cb_)
                order_cb_(completed);
        } else {
            ++it;
        }
    }

    portfolio_.recalc_equity();
}

void SimBroker::try_fill(Order& order, const Bar& bar) {
    double fill_price = 0.0;

    if (order.type == OrderType::MARKET) {
        // 市价单: 以 open 价 + 滑点成交
        fill_price = bar.open;
        if (order.side == Side::BUY)
            fill_price *= (1.0 + config_.slippage);
        else
            fill_price *= (1.0 - config_.slippage);
    } else {
        // 限价单: 检查价格是否在 bar 的范围内
        if (order.side == Side::BUY) {
            if (bar.low <= order.price)
                fill_price = order.price;
            else
                return;  // 未触及
        } else {
            if (bar.high >= order.price)
                fill_price = order.price;
            else
                return;
        }
    }

    // 买入检查可用资金
    if (order.side == Side::BUY) {
        double cost = fill_price * order.quantity;
        double buy_fee = (order.type == OrderType::MARKET && config_.taker_fee >= 0)
            ? config_.taker_fee
            : (order.type == OrderType::LIMIT && config_.maker_fee >= 0)
                ? config_.maker_fee : config_.commission_rate;
        double commission = cost * buy_fee;
        if (portfolio_.cash() < cost + commission) {
            if (order.type == OrderType::MARKET)
                order.status = OrderStatus::CANCELLED;  // 市价单无法等待，直接取消
            return;
        }
    }

    // 卖出检查持仓，防止超卖
    if (order.side == Side::SELL) {
        double held = portfolio_.position(order.symbol_id).quantity;
        if (held <= 0) {
            if (order.type == OrderType::MARKET)
                order.status = OrderStatus::CANCELLED;
            return;
        }
        if (order.quantity > held)
            order.quantity = held;  // clamp 到实际持仓
    }

    // maker/taker 费率：限价单用 maker，市价单用 taker
    double fee_rate = config_.commission_rate;
    if (order.type == OrderType::LIMIT && config_.maker_fee >= 0)
        fee_rate = config_.maker_fee;
    else if (order.type == OrderType::MARKET && config_.taker_fee >= 0)
        fee_rate = config_.taker_fee;

    auto fill = portfolio_.fill_order(order, fill_price, bar.timestamp_ms, fee_rate);
    fills_.push_back(fill);
}

}  // namespace qe
