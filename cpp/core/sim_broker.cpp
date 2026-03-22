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
    // 回调延迟到遍历结束后执行，避免回调中 submit_order 导致迭代器失效
    std::vector<Order> completed_orders;
    auto it = pending_orders_.begin();
    while (it != pending_orders_.end()) {
        if (it->symbol_id != symbol_id) {
            ++it;
            continue;
        }
        try_fill(*it, bar);
        if (it->status == OrderStatus::FILLED || it->status == OrderStatus::CANCELLED) {
            completed_orders.push_back(*it);
            it = pending_orders_.erase(it);
        } else {
            ++it;
        }
    }

    portfolio_.recalc_equity();

    // 延迟触发回调（安全：pending_orders_ 不再被遍历）
    if (order_cb_) {
        for (const auto& order : completed_orders)
            order_cb_(order);
    }
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
    } else if (order.type == OrderType::LIMIT) {
        // 限价单: 检查价格是否在 bar 的范围内
        if (order.side == Side::BUY) {
            if (bar.low <= order.price)
                fill_price = order.price;
            else
                return;
        } else {
            if (bar.high >= order.price)
                fill_price = order.price;
            else
                return;
        }
    } else if (order.type == OrderType::STOP_MARKET) {
        // 止损市价单: 价格触及 stop_price 后以 stop_price + 滑点成交
        // 卖出止损: bar.low <= stop_price 触发
        // 买入止损: bar.high >= stop_price 触发
        if (order.side == Side::SELL) {
            if (bar.low <= order.stop_price)
                fill_price = order.stop_price * (1.0 - config_.slippage);
            else
                return;
        } else {
            if (bar.high >= order.stop_price)
                fill_price = order.stop_price * (1.0 + config_.slippage);
            else
                return;
        }
    } else if (order.type == OrderType::STOP_LIMIT) {
        // 止损限价单: 价格触及 stop_price 后，以 limit_price 挂限价单
        // 简化实现: 如果同一根 bar 内 stop 触发且 limit 可成交，则成交
        if (order.side == Side::SELL) {
            if (bar.low <= order.stop_price && bar.high >= order.price)
                fill_price = order.price;
            else
                return;
        } else {
            if (bar.high >= order.stop_price && bar.low <= order.price)
                fill_price = order.price;
            else
                return;
        }
    }

    // 买入检查可用资金
    if (order.side == Side::BUY) {
        double cost = fill_price * order.quantity;
        bool buy_is_maker = (order.type == OrderType::LIMIT || order.type == OrderType::STOP_LIMIT);
        double buy_fee = (buy_is_maker && config_.maker_fee >= 0)
            ? config_.maker_fee
            : (!buy_is_maker && config_.taker_fee >= 0)
                ? config_.taker_fee : config_.commission_rate;
        double commission = cost * buy_fee;
        bool is_immediate = (order.type == OrderType::MARKET || order.type == OrderType::STOP_MARKET);
        if (portfolio_.cash() < cost + commission) {
            if (is_immediate)
                order.status = OrderStatus::CANCELLED;
            return;
        }
    }

    // 卖出检查持仓，防止超卖
    if (order.side == Side::SELL) {
        double held = portfolio_.position(order.symbol_id).quantity;
        bool is_immediate = (order.type == OrderType::MARKET || order.type == OrderType::STOP_MARKET);
        if (held <= 0) {
            if (is_immediate)
                order.status = OrderStatus::CANCELLED;
            return;
        }
        if (order.quantity > held)
            order.quantity = held;  // clamp 到实际持仓
    }

    // volume 参与率限制: 单笔订单不超过 bar volume 的 max_volume_pct
    if (config_.max_volume_pct > 0 && bar.volume > 0) {
        double max_qty_by_value = bar.volume * config_.max_volume_pct;
        // volume 是 base asset 数量，直接比较 quantity
        if (order.quantity > max_qty_by_value) {
            order.quantity = max_qty_by_value;
            if (order.quantity < 1e-12)
                return;  // 太小，跳过
        }
    }

    // maker/taker 费率：限价类用 maker，市价/止损市价用 taker
    double fee_rate = config_.commission_rate;
    bool is_maker = (order.type == OrderType::LIMIT || order.type == OrderType::STOP_LIMIT);
    if (is_maker && config_.maker_fee >= 0)
        fee_rate = config_.maker_fee;
    else if (!is_maker && config_.taker_fee >= 0)
        fee_rate = config_.taker_fee;

    auto fill = portfolio_.fill_order(order, fill_price, bar.timestamp_ms, fee_rate);
    fills_.push_back(fill);
}

}  // namespace qe
