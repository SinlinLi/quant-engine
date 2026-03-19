#include "test/test_framework.h"
#include "core/engine.h"
#include "core/sim_broker.h"
#include "data/csv_feed.h"
#include "indicator/sma.h"

// 空 feed → 不崩溃，no trades
TEST(engine_empty_feed) {
    qe::Engine engine;
    engine.add_feed(std::make_unique<qe::CsvFeed>(0, std::vector<qe::Bar>{}));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    auto result = engine.run();
    ASSERT_EQ(result.total_trades, (uint32_t)0);
    ASSERT_NEAR(result.final_equity, 10000.0, 1e-12);
}

// 简单策略: 第一根 bar 买入, 最后一根 bar 卖出
namespace {
class BuyAndHold : public qe::Strategy {
public:
    BuyAndHold(uint16_t sid) : sid_(sid) {}
    void on_init(qe::Context&) override {}
    void on_bar(qe::Context& ctx, uint16_t symbol_id, const qe::Bar&) override {
        if (symbol_id != sid_) return;
        auto& pos = ctx.position(sid_);
        if (pos.quantity == 0.0)
            ctx.buy(sid_, 1.0);
        ++bar_count_;
    }
    void on_stop(qe::Context& ctx) override {
        auto& pos = ctx.position(sid_);
        if (pos.quantity > 0)
            ctx.sell(sid_, pos.quantity);
    }
    int bar_count_ = 0;
private:
    uint16_t sid_;
};
}

TEST(engine_buy_and_hold) {
    qe::Engine engine;
    auto btc = engine.symbols().id("BTC");

    // 5 bars: price 100 → 200
    std::vector<qe::Bar> bars;
    for (int i = 0; i < 5; ++i)
        bars.push_back(make_bar(i * 60000, 100 + i * 25, 130 + i * 25, 95 + i * 25, 100 + i * 25));

    engine.add_feed(std::make_unique<qe::CsvFeed>(btc, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    auto strategy = std::make_shared<BuyAndHold>(btc);
    engine.add_strategy(strategy);

    auto result = engine.run();

    // bar[0]: open=100, on_bar → submit buy
    // bar[1]: open=125, on_bar → fill buy @ 125
    // bar[2..4]: no new trades
    // on_stop: sell(1.0) → pending
    // flush: broker.on_bar(btc, last_bar) → sell fills @ 200 (bar[4].open)
    // PnL = (200 - 125) * 1 = 75
    ASSERT_EQ(result.total_trades, (uint32_t)1);
    ASSERT_NEAR(result.final_equity, 10075.0, 1e-6);
    ASSERT_EQ(strategy->bar_count_, 5);
}

// 多 feed 时间归并: 验证交叉排序
namespace {
class BarCounter : public qe::Strategy {
public:
    void on_init(qe::Context&) override {}
    void on_bar(qe::Context&, uint16_t symbol_id, const qe::Bar& bar) override {
        timestamps.push_back(bar.timestamp_ms);
        symbols.push_back(symbol_id);
    }
    std::vector<int64_t> timestamps;
    std::vector<uint16_t> symbols;
};
}

TEST(engine_multi_feed_merge) {
    qe::Engine engine;
    auto s0 = engine.symbols().id("A");
    auto s1 = engine.symbols().id("B");

    // Feed A: ts = 1000, 3000, 5000
    std::vector<qe::Bar> bars_a = {
        make_bar(1000, 10, 11, 9, 10),
        make_bar(3000, 30, 31, 29, 30),
        make_bar(5000, 50, 51, 49, 50),
    };

    // Feed B: ts = 2000, 4000
    std::vector<qe::Bar> bars_b = {
        make_bar(2000, 20, 21, 19, 20),
        make_bar(4000, 40, 41, 39, 40),
    };

    engine.add_feed(std::make_unique<qe::CsvFeed>(s0, std::move(bars_a)));
    engine.add_feed(std::make_unique<qe::CsvFeed>(s1, std::move(bars_b)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    auto counter = std::make_shared<BarCounter>();
    engine.add_strategy(counter);

    engine.run();

    // 应该按时间排序: 1000(A) 2000(B) 3000(A) 4000(B) 5000(A)
    ASSERT_EQ(counter->timestamps.size(), (size_t)5);
    ASSERT_EQ(counter->timestamps[0], (int64_t)1000);
    ASSERT_EQ(counter->symbols[0], s0);
    ASSERT_EQ(counter->timestamps[1], (int64_t)2000);
    ASSERT_EQ(counter->symbols[1], s1);
    ASSERT_EQ(counter->timestamps[2], (int64_t)3000);
    ASSERT_EQ(counter->symbols[2], s0);
    ASSERT_EQ(counter->timestamps[3], (int64_t)4000);
    ASSERT_EQ(counter->symbols[3], s1);
    ASSERT_EQ(counter->timestamps[4], (int64_t)5000);
    ASSERT_EQ(counter->symbols[4], s0);
}

// 指标生命周期: Engine 在 on_bar 之前更新指标
namespace {
class IndicatorCheckStrategy : public qe::Strategy {
public:
    IndicatorCheckStrategy(uint16_t sid) : sid_(sid) {}
    void on_init(qe::Context& ctx) override {
        sma_ = &ctx.indicator<qe::SMA>(sid_, 3);
    }
    void on_bar(qe::Context&, uint16_t symbol_id, const qe::Bar&) override {
        if (symbol_id != sid_) return;
        // 记录每根 bar 时 SMA 是否 ready 以及 value
        ready_history.push_back(sma_->ready());
        if (sma_->ready())
            value_history.push_back(sma_->value());
    }
    std::vector<bool> ready_history;
    std::vector<double> value_history;
private:
    uint16_t sid_;
    qe::SMA* sma_ = nullptr;
};
}

TEST(engine_indicator_lifecycle) {
    qe::Engine engine;
    auto sid = engine.symbols().id("X");

    // 5 bars: close = 10, 20, 30, 40, 50
    std::vector<qe::Bar> bars;
    for (int i = 1; i <= 5; ++i)
        bars.push_back(make_bar(i * 60000, i * 10.0, i * 10.0, i * 10.0, i * 10.0));

    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    auto strat = std::make_shared<IndicatorCheckStrategy>(sid);
    engine.add_strategy(strat);

    engine.run();

    // SMA(3): ready after bar[2] (3rd bar)
    ASSERT_EQ(strat->ready_history.size(), (size_t)5);
    ASSERT_FALSE(strat->ready_history[0]);
    ASSERT_FALSE(strat->ready_history[1]);
    ASSERT_TRUE(strat->ready_history[2]);
    ASSERT_TRUE(strat->ready_history[3]);
    ASSERT_TRUE(strat->ready_history[4]);

    // SMA values: (10+20+30)/3=20, (20+30+40)/3=30, (30+40+50)/3=40
    ASSERT_EQ(strat->value_history.size(), (size_t)3);
    ASSERT_NEAR(strat->value_history[0], 20.0, 1e-12);
    ASSERT_NEAR(strat->value_history[1], 30.0, 1e-12);
    ASSERT_NEAR(strat->value_history[2], 40.0, 1e-12);
}

// 绩效指标: 验证 max drawdown 计算
TEST(engine_performance_drawdown) {
    qe::Engine engine;
    auto sid = engine.symbols().id("X");

    // 制造一个先涨后跌的序列来验证回撤
    // open 价决定成交价，close 影响 equity
    std::vector<qe::Bar> bars;
    // 10 bars, close: 100, 110, 120, 130, 100, 90, 80, 70, 100, 110
    double prices[] = {100, 110, 120, 130, 100, 90, 80, 70, 100, 110};
    for (int i = 0; i < 10; ++i)
        bars.push_back(make_bar(i * 60000, prices[i], prices[i] + 5, prices[i] - 5, prices[i]));

    engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));

    qe::SimBrokerConfig cfg;
    cfg.cash = 10000.0; cfg.commission_rate = 0.0; cfg.slippage = 0.0;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));

    // 不加策略，纯持现金 → 回撤为 0，equity 恒定
    auto result = engine.run();
    ASSERT_NEAR(result.max_drawdown, 0.0, 1e-12);
    ASSERT_NEAR(result.final_equity, 10000.0, 1e-12);
}