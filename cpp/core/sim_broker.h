#pragma once

#include <vector>
#include "core/broker.h"
#include "core/portfolio.h"

namespace qe {

struct SimBrokerConfig {
    double cash = 10000.0;
    double commission_rate = 0.0004;  // 统一费率（向后兼容，优先级低于 maker/taker）
    double maker_fee = -1.0;          // maker 费率（<0 表示使用 commission_rate）
    double taker_fee = -1.0;          // taker 费率（<0 表示使用 commission_rate）
    double slippage = 0.0;            // 滑点比例
    double max_volume_pct = 0.0;      // 最大 volume 参与率（0=无限制，如 0.1=不超过 bar volume 的 10%）
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

private:
    SimBrokerConfig config_;
    Portfolio portfolio_;
    std::vector<Order> pending_orders_;
    std::vector<FillEvent> fills_;
    uint64_t next_order_id_ = 1;

    void try_fill(Order& order, const Bar& bar);
};

}  // namespace qe
