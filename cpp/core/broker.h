#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include "core/order.h"
#include "data/bar.h"

namespace qe {

class Broker {
public:
    virtual ~Broker() = default;
    virtual uint64_t submit_order(Order order) = 0;
    virtual bool cancel_order(uint64_t order_id) = 0;
    virtual const Position& position(uint16_t symbol_id) const = 0;
    virtual double equity() const = 0;
    virtual double available_cash() const = 0;
    virtual void on_bar(uint16_t symbol_id, const Bar& bar) = 0;

    // 获取所有已成交记录
    virtual const std::vector<FillEvent>& fills() const = 0;

    // 取消所有 pending 订单（回测结束前清理用）
    virtual void cancel_all_pending() = 0;

    // 订单状态变更回调（Engine 设置，转发到 Strategy::on_order）
    using OrderCallback = std::function<void(const Order&)>;
    void set_order_callback(OrderCallback cb) { order_cb_ = std::move(cb); }

protected:
    OrderCallback order_cb_;
};

}  // namespace qe
