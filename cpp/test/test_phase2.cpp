// Phase 2.2 测试：止损单、volume 参与率、绩效指标

#include "test/test_framework.h"
#include "core/engine.h"
#include "core/sim_broker.h"
#include "data/csv_feed.h"

// ─── 止损市价单 (STOP_MARKET) ───

TEST(stop_market_sell_triggered) {
    auto broker = make_broker(10000.0);

    // 先买入
    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));
    // 持仓 1@100

    // 止损卖单 @ stop=95
    qe::Order stop;
    stop.symbol_id = 0; stop.side = qe::Side::SELL;
    stop.type = qe::OrderType::STOP_MARKET;
    stop.stop_price = 95.0; stop.quantity = 1.0;
    broker.submit_order(stop);

    // bar: low=96 → 未触发
    broker.on_bar(0, make_bar(2000, 102, 104, 96, 100));
    ASSERT_NEAR(broker.position(0).quantity, 1.0, 1e-12);

    // bar: low=90 → 触发 @ 95 * (1 - slippage)
    broker.on_bar(0, make_bar(3000, 98, 99, 90, 92));
    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
    ASSERT_NEAR(broker.fills().back().price, 95.0, 1e-12);  // slippage=0
}

TEST(stop_market_sell_with_slippage) {
    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.01;
    qe::SimBroker broker(cfg);

    // 买入
    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    // 止损 @ 95 with 1% slippage
    qe::Order stop;
    stop.symbol_id = 0; stop.side = qe::Side::SELL;
    stop.type = qe::OrderType::STOP_MARKET;
    stop.stop_price = 95.0; stop.quantity = 1.0;
    broker.submit_order(stop);

    broker.on_bar(0, make_bar(2000, 98, 99, 88, 90));
    // fill_price = 95 * (1 - 0.01) = 94.05
    ASSERT_NEAR(broker.fills().back().price, 94.05, 1e-12);
}

TEST(stop_market_not_triggered) {
    auto broker = make_broker(10000.0);

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    qe::Order stop;
    stop.symbol_id = 0; stop.side = qe::Side::SELL;
    stop.type = qe::OrderType::STOP_MARKET;
    stop.stop_price = 80.0; stop.quantity = 1.0;
    broker.submit_order(stop);

    // price never goes below 80
    broker.on_bar(0, make_bar(2000, 105, 115, 85, 110));
    ASSERT_NEAR(broker.position(0).quantity, 1.0, 1e-12);  // still holding
}

// ─── 止损限价单 (STOP_LIMIT) ───

TEST(stop_limit_sell_triggered) {
    auto broker = make_broker(10000.0);

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    // 止损限价: stop=95, limit=93
    // 触发条件: bar.low <= 95, 成交条件: bar.high >= 93
    qe::Order stop;
    stop.symbol_id = 0; stop.side = qe::Side::SELL;
    stop.type = qe::OrderType::STOP_LIMIT;
    stop.stop_price = 95.0; stop.price = 93.0; stop.quantity = 1.0;
    broker.submit_order(stop);

    // bar: low=94 (触发), high=96 (>= limit 93, 可成交) → 成交 @ 93
    broker.on_bar(0, make_bar(2000, 97, 97, 94, 95));
    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
    ASSERT_NEAR(broker.fills().back().price, 93.0, 1e-12);
}

// ─── volume 参与率限制 ───

TEST(volume_participation_limit) {
    qe::SimBrokerConfig cfg;
    cfg.cash = 100000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    cfg.max_volume_pct = 0.1;  // 最多占 bar volume 的 10%
    qe::SimBroker broker(cfg);

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 50.0;  // 想买 50
    broker.submit_order(buy);

    // bar volume = 100, 10% = 10
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105, 100.0));

    ASSERT_EQ(broker.fills().size(), (size_t)1);
    // 只成交 10（volume 限制）
    ASSERT_NEAR(broker.fills().back().quantity, 10.0, 1e-12);
    ASSERT_NEAR(broker.position(0).quantity, 10.0, 1e-12);
}

TEST(volume_no_limit_when_zero) {
    qe::SimBrokerConfig cfg;
    cfg.cash = 100000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    cfg.max_volume_pct = 0.0;  // 无限制
    qe::SimBroker broker(cfg);

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 50.0;
    broker.submit_order(buy);

    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105, 10.0));  // volume=10

    // max_volume_pct=0 → 无限制，全量成交
    ASSERT_NEAR(broker.fills().back().quantity, 50.0, 1e-12);
}

// ─── 绩效指标 ───

namespace {
class SwingTrader : public qe::Strategy {
public:
    SwingTrader(uint16_t sid) : sid_(sid) {}
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar&) override {
        if (sid != sid_) return;
        ++n_;
        auto& pos = ctx.position(sid_);
        // 第 2 根 bar 买入，第 6 根 bar 卖出
        if (n_ == 2 && pos.quantity < 1e-12) ctx.buy(sid_, 1.0);
        if (n_ == 6 && pos.quantity > 1e-12) ctx.sell(sid_, pos.quantity);
    }
    void on_stop(qe::Context& ctx) override {
        auto& pos = ctx.position(sid_);
        if (pos.quantity > 1e-12) ctx.sell(sid_, pos.quantity);
    }
private:
    uint16_t sid_;
    int n_ = 0;
};
}

TEST(performance_sortino_calmar_profit_factor) {
    qe::Engine engine;
    auto sid = engine.symbols().id("BTC");

    // 10 bars: 先涨后跌再涨，制造有意义的收益序列
    double prices[] = {100, 110, 120, 130, 140, 120, 100, 90, 105, 115};
    std::vector<qe::Bar> bars;
    for (int i = 0; i < 10; ++i)
        bars.push_back(make_bar(i * 3600000, prices[i], prices[i] + 5,
                                prices[i] - 5, prices[i]));
    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.001; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));
    engine.add_strategy(std::make_shared<SwingTrader>(sid));

    auto result = engine.run();

    // 基本验证：指标有值（不是全 0 或 NaN）
    ASSERT_TRUE(result.sharpe == result.sharpe);  // not NaN
    ASSERT_TRUE(result.sortino == result.sortino);
    ASSERT_TRUE(result.calmar == result.calmar);

    // equity curve 有内容
    ASSERT_GT(result.equity_curve.size(), (size_t)5);

    // total_trades > 0
    ASSERT_GT(result.total_trades, (uint32_t)0);

    // profit_factor >= 0
    ASSERT_TRUE(result.profit_factor >= 0.0);
}

// ─── 止损单集成测试：策略使用 stop_loss ───

namespace {
class StopLossStrategy : public qe::Strategy {
public:
    StopLossStrategy(uint16_t sid) : sid_(sid) {}
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar& bar) override {
        if (sid != sid_) return;
        auto& pos = ctx.position(sid_);
        if (pos.quantity < 1e-12 && !bought_) {
            ctx.buy(sid_, 1.0);
            bought_ = true;
        }
    }
    void on_order(qe::Context& ctx, const qe::Order& order) override {
        // 买入成交后立即挂止损（用持仓均价计算止损价）
        if (order.side == qe::Side::BUY && order.status == qe::OrderStatus::FILLED) {
            double entry = ctx.position(order.symbol_id).avg_entry_price;
            ctx.stop_loss(order.symbol_id, entry * 0.95, order.filled_quantity);
        }
    }
    bool stop_triggered = false;
private:
    uint16_t sid_;
    bool bought_ = false;
};
}

TEST(stop_loss_integration) {
    qe::Engine engine;
    auto sid = engine.symbols().id("BTC");

    // 先涨后崩
    std::vector<qe::Bar> bars = {
        make_bar(0,     100, 105, 95, 100),    // bar[0]: 提交 buy
        make_bar(60000, 100, 110, 95, 105),    // bar[1]: buy 成交 @ 100, on_order 挂止损 @ 95
        make_bar(120000, 105, 108, 102, 106),  // bar[2]: 正常
        make_bar(180000, 106, 107, 103, 105),  // bar[3]: 正常
        make_bar(240000, 80, 90, 70, 75),      // bar[4]: 崩盘！low=70 < 95 → 止损触发
        make_bar(300000, 75, 78, 72, 76),      // bar[5]: 已无持仓
    };
    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    auto strat = std::make_shared<StopLossStrategy>(sid);
    engine.add_strategy(strat);

    auto result = engine.run();

    // 买入 @ 100, 止损触发 @ 95, pnl = (95-100)*1 = -5
    ASSERT_EQ(result.total_trades, (uint32_t)1);
    ASSERT_NEAR(result.final_equity, 9995.0, 1e-6);
}
