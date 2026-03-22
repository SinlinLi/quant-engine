// P0 正确性测试：会计恒等式、on_stop close 价、on_order 回调、
// quantity<=0 校验、equity_curve 采样、win_rate 扣佣金、maker/taker 费率

#include "test/test_framework.h"
#include "core/engine.h"
#include "core/sim_broker.h"
#include "data/csv_feed.h"
#include "indicator/sma.h"

// ─── on_stop 以 close 价成交 ───

namespace {
class SellOnStop : public qe::Strategy {
public:
    SellOnStop(uint16_t sid) : sid_(sid) {}
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar&) override {
        if (sid != sid_) return;
        auto& pos = ctx.position(sid_);
        if (pos.quantity < 1e-12)
            ctx.buy(sid_, 1.0);
    }
    void on_stop(qe::Context& ctx) override {
        auto& pos = ctx.position(sid_);
        if (pos.quantity > 1e-12)
            ctx.sell(sid_, pos.quantity);
    }
private:
    uint16_t sid_;
};
}

TEST(on_stop_fills_at_close_price) {
    qe::Engine engine;
    auto sid = engine.symbols().id("BTC");

    // 3 bars, 最后一根 open=100, close=150（故意不同）
    std::vector<qe::Bar> bars = {
        make_bar(1000, 80, 85, 75, 80),    // bar[0]: 策略提交 buy
        make_bar(2000, 90, 95, 85, 90),    // bar[1]: buy 成交 @ open=90
        make_bar(3000, 100, 160, 95, 150), // bar[2]: last bar, open=100, close=150
    };
    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));
    engine.add_strategy(std::make_shared<SellOnStop>(sid));

    auto result = engine.run();

    // buy @ bar[1].open = 90, sell on_stop @ bar[2].close = 150
    // pnl = (150 - 90) * 1 = 60
    ASSERT_NEAR(result.final_equity, 10060.0, 1e-6);
    ASSERT_EQ(result.total_trades, (uint32_t)1);
}

// ─── 会计恒等式：initial_cash + pnl - commission == final_equity ───

namespace {
class TradingStrategy : public qe::Strategy {
public:
    TradingStrategy(uint16_t sid) : sid_(sid) {}
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar& bar) override {
        if (sid != sid_) return;
        ++count_;
        auto& pos = ctx.position(sid_);
        if (count_ == 2 && pos.quantity < 1e-12)
            ctx.buy(sid_, 0.5);
        if (count_ == 5 && pos.quantity > 1e-12)
            ctx.sell(sid_, pos.quantity);
        if (count_ == 7 && pos.quantity < 1e-12)
            ctx.buy(sid_, 0.3);
    }
    void on_stop(qe::Context& ctx) override {
        auto& pos = ctx.position(sid_);
        if (pos.quantity > 1e-12)
            ctx.sell(sid_, pos.quantity);
    }
private:
    uint16_t sid_;
    int count_ = 0;
};
}

TEST(accounting_identity) {
    qe::Engine engine;
    auto sid = engine.symbols().id("BTC");

    // 10 bars with varying prices
    double prices[] = {100, 105, 110, 115, 120, 108, 95, 102, 110, 115};
    std::vector<qe::Bar> bars;
    for (int i = 0; i < 10; ++i)
        bars.push_back(make_bar(i * 60000, prices[i], prices[i] + 5,
                                prices[i] - 5, prices[i]));
    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.001; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));
    engine.add_strategy(std::make_shared<TradingStrategy>(sid));

    auto result = engine.run();

    // final_equity == equity_curve 最后一个值
    ASSERT_NEAR(result.final_equity, result.equity_curve.back(), 1e-6);

    // total_return 一致性
    double expected_return = (result.final_equity - result.initial_cash) / result.initial_cash;
    ASSERT_NEAR(result.total_return, expected_return, 1e-12);

    // 有交易发生
    ASSERT_GT(result.total_trades, (uint32_t)0);

    // equity curve 不应有 NaN 或负值
    for (double eq : result.equity_curve) {
        ASSERT_TRUE(eq > 0);
        ASSERT_TRUE(eq == eq);  // NaN check
    }
}

// ─── equity_curve 长度 == 总 bar 数 + 1 (flush 后的额外采样点) ───

TEST(equity_curve_length) {
    qe::Engine engine;
    auto sid = engine.symbols().id("X");

    int N = 7;
    std::vector<qe::Bar> bars;
    for (int i = 0; i < N; ++i)
        bars.push_back(make_bar(i * 60000, 100, 105, 95, 100));
    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    auto result = engine.run();

    // N bars 主循环 + 1 flush 后采样
    ASSERT_EQ(result.equity_curve.size(), (size_t)(N + 1));
}

// ─── on_order 回调正确触发 ───

namespace {
class OrderTracker : public qe::Strategy {
public:
    OrderTracker(uint16_t sid) : sid_(sid) {}
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar&) override {
        if (sid != sid_) return;
        ++bar_count_;
        if (bar_count_ == 1)
            ctx.buy(sid_, 1.0);
        if (bar_count_ == 4) {
            auto& pos = ctx.position(sid_);
            if (pos.quantity > 1e-12)
                ctx.sell(sid_, pos.quantity);
        }
    }
    void on_order(qe::Context&, const qe::Order& order) override {
        orders_received.push_back(order);
    }
    std::vector<qe::Order> orders_received;
private:
    uint16_t sid_;
    int bar_count_ = 0;
};
}

TEST(on_order_callback_fired) {
    qe::Engine engine;
    auto sid = engine.symbols().id("BTC");

    std::vector<qe::Bar> bars;
    for (int i = 0; i < 6; ++i)
        bars.push_back(make_bar(i * 60000, 100 + i * 10, 110 + i * 10,
                                95 + i * 10, 100 + i * 10));
    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    auto tracker = std::make_shared<OrderTracker>(sid);
    engine.add_strategy(tracker);
    engine.run();

    // 应该收到 2 次 on_order：buy fill + sell fill
    ASSERT_EQ(tracker->orders_received.size(), (size_t)2);
    ASSERT_EQ(tracker->orders_received[0].side, qe::Side::BUY);
    ASSERT_EQ(tracker->orders_received[0].status, qe::OrderStatus::FILLED);
    ASSERT_EQ(tracker->orders_received[1].side, qe::Side::SELL);
    ASSERT_EQ(tracker->orders_received[1].status, qe::OrderStatus::FILLED);
}

// ─── quantity <= 0 订单拒绝 ───

TEST(submit_zero_quantity_rejected) {
    auto broker = make_broker(10000.0);

    qe::Order order;
    order.symbol_id = 0; order.side = qe::Side::BUY;
    order.type = qe::OrderType::MARKET;

    order.quantity = 0.0;
    ASSERT_EQ(broker.submit_order(order), (uint64_t)0);

    order.quantity = -1.0;
    ASSERT_EQ(broker.submit_order(order), (uint64_t)0);

    // 正常订单应该成功
    order.quantity = 1.0;
    ASSERT_GT(broker.submit_order(order), (uint64_t)0);
}

// ─── win_rate 扣双边佣金 ───

namespace {
class MarginalTrader : public qe::Strategy {
public:
    MarginalTrader(uint16_t sid) : sid_(sid) {}
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar&) override {
        if (sid != sid_) return;
        ++count_;
        auto& pos = ctx.position(sid_);
        if (count_ == 1) ctx.buy(sid_, 1.0);   // buy
        if (count_ == 3) ctx.sell(sid_, 1.0);   // sell
    }
private:
    uint16_t sid_;
    int count_ = 0;
};
}

TEST(win_rate_deducts_both_commissions) {
    qe::Engine engine;
    auto sid = engine.symbols().id("BTC");

    // 买入 @ 100 (bar[1].open), 卖出 @ 102 (bar[3].open)
    // gross pnl = (102 - 100) * 1 = 2.0
    // buy commission = 100 * 1 * 0.01 = 1.0
    // sell commission = 102 * 1 * 0.01 = 1.02
    // net pnl = 2.0 - 1.0 - 1.02 = -0.02 → 这是亏损
    std::vector<qe::Bar> bars = {
        make_bar(0,     100, 105, 95, 100),  // bar[0]: 策略提交 buy
        make_bar(60000, 100, 105, 95, 100),  // bar[1]: buy 成交 @ 100
        make_bar(120000, 102, 107, 97, 102), // bar[2]: 策略提交 sell
        make_bar(180000, 102, 107, 97, 102), // bar[3]: sell 成交 @ 102
    };
    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.01; cfg.slippage = 0.0;  // 1% 佣金
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));
    engine.add_strategy(std::make_shared<MarginalTrader>(sid));

    auto result = engine.run();

    // gross pnl=2 > 0 但 net pnl < 0，win_rate 应该是 0
    ASSERT_EQ(result.total_trades, (uint32_t)1);
    ASSERT_NEAR(result.win_rate, 0.0, 1e-12);
}

// ─── maker/taker 费率分离 ───

TEST(maker_taker_fee_split) {
    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0;
    cfg.maker_fee = 0.001;   // 0.1% maker
    cfg.taker_fee = 0.005;   // 0.5% taker
    cfg.slippage = 0.0;
    qe::SimBroker broker(cfg);

    // 市价买单 → taker 费率
    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    // taker commission = 100 * 1 * 0.005 = 0.5
    ASSERT_NEAR(broker.fills().back().commission, 0.5, 1e-12);

    // 限价卖单 → maker 费率
    qe::Order sell;
    sell.symbol_id = 0; sell.side = qe::Side::SELL;
    sell.type = qe::OrderType::LIMIT;
    sell.price = 115.0; sell.quantity = 1.0;
    broker.submit_order(sell);
    broker.on_bar(0, make_bar(2000, 112, 118, 111, 116));

    // maker commission = 115 * 1 * 0.001 = 0.115
    ASSERT_NEAR(broker.fills().back().commission, 0.115, 1e-12);
}

// ─── 僵尸订单清理：市价单资金不足被 CANCELLED ───

TEST(zombie_market_order_cancelled) {
    auto broker = make_broker(50.0);  // 只有 50 块

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);

    // open=100，需要 100 > 50，市价单应被 cancel
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));
    ASSERT_EQ(broker.fills().size(), (size_t)0);

    // 下一根 bar 也不应该成交（订单已被清理）
    broker.on_bar(0, make_bar(2000, 30, 35, 25, 30));
    ASSERT_EQ(broker.fills().size(), (size_t)0);
    ASSERT_NEAR(broker.available_cash(), 50.0, 1e-12);
}

// ─── 卖出滑点测试 ───

TEST(sell_slippage) {
    auto broker = make_broker(10000.0, 0.0, 0.01);  // 1% 滑点

    // 先买入
    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    // 卖出
    qe::Order sell;
    sell.symbol_id = 0; sell.side = qe::Side::SELL;
    sell.type = qe::OrderType::MARKET; sell.quantity = 1.0;
    broker.submit_order(sell);
    broker.on_bar(0, make_bar(2000, 120, 130, 115, 125));

    // sell fill_price = 120 * (1 - 0.01) = 118.8
    ASSERT_NEAR(broker.fills().back().price, 118.8, 1e-12);
}

// ─── 多次加仓均价 ───

TEST(multiple_buys_avg_price) {
    auto broker = make_broker(100000.0);

    // 第一次买 1@100
    qe::Order buy1;
    buy1.symbol_id = 0; buy1.side = qe::Side::BUY;
    buy1.type = qe::OrderType::MARKET; buy1.quantity = 1.0;
    broker.submit_order(buy1);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    // 第二次买 1@200
    qe::Order buy2;
    buy2.symbol_id = 0; buy2.side = qe::Side::BUY;
    buy2.type = qe::OrderType::MARKET; buy2.quantity = 1.0;
    broker.submit_order(buy2);
    broker.on_bar(0, make_bar(2000, 200, 210, 190, 205));

    // avg_entry_price = (100 + 200) / 2 = 150
    ASSERT_NEAR(broker.position(0).quantity, 2.0, 1e-12);
    ASSERT_NEAR(broker.position(0).avg_entry_price, 150.0, 1e-12);
}
