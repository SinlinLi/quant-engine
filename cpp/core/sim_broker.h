#pragma once

#include <vector>
#include "core/broker.h"
#include "core/portfolio.h"

namespace qe {

struct SimBrokerConfig {
    double cash = 10000.0;
    double commission_rate = 0.0004;  // 万四
    double slippage = 0.0;            // 滑点比例
};

class SimBroker : public Broker {
public:
    explicit SimBroker(const SimBrokerConfig& config);

    uint64_t submit_order(Order order) override;
    bool cancel_order(uint64_t order_id) override;
    const Position& position(uint16_t symbol_id) const override;
    double equity() const override;
    double available_cash() const override;
    void on_bar(uint16_t symbol_id, const Bar& bar) override;
    const std::vector<FillEvent>& fills() const override { return fills_; }
    void cancel_all_pending() override { pending_orders_.clear(); }

    // on_bar 成交的订单通知回调（Engine 设置）
    using OrderCallback = void(*)(const Order&, void*);
    void set_order_callback(OrderCallback cb, void* ctx) {
        order_cb_ = cb;
        order_cb_ctx_ = ctx;
    }

private:
    SimBrokerConfig config_;
    Portfolio portfolio_;
    std::vector<Order> pending_orders_;
    std::vector<FillEvent> fills_;
    uint64_t next_order_id_ = 1;
    OrderCallback order_cb_ = nullptr;
    void* order_cb_ctx_ = nullptr;

    void try_fill(Order& order, const Bar& bar);
};

}  // namespace qe
