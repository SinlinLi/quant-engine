#include <cstdio>
#include <cmath>
#include <memory>
#include <vector>

#include "core/engine.h"
#include "core/sim_broker.h"
#include "data/csv_feed.h"
#include "strategies/dual_ma.h"

using namespace qe;

// 生成模拟 K 线数据（正弦波 + 趋势 + 噪声）
static std::vector<Bar> generate_test_data(int count, double base_price) {
    std::vector<Bar> bars;
    bars.reserve(count);

    double price = base_price;
    int64_t ts = 1672531200000LL;  // 2023-01-01 00:00:00 UTC

    for (int i = 0; i < count; ++i) {
        // 正弦波 + 微弱上升趋势 + 伪随机噪声
        double wave = std::sin(i * 0.01) * base_price * 0.05;
        double trend = i * base_price * 0.000001;
        double noise = ((i * 7 + 13) % 100 - 50) * base_price * 0.0001;
        price = base_price + wave + trend + noise;

        double hl_range = price * 0.005;  // 0.5% 振幅
        Bar bar;
        bar.timestamp_ms = ts + i * 60000LL;  // 1 分钟间隔
        bar.open = price - hl_range * 0.2;
        bar.high = price + hl_range;
        bar.low = price - hl_range;
        bar.close = price;
        bar.volume = 100.0 + (i % 50) * 2.0;
        bar.quote_volume = bar.volume * price;
        bars.push_back(bar);
    }
    return bars;
}

int main() {
    printf("=== Quant Engine Backtest ===\n\n");

    Engine engine;

    // 注册 symbol
    auto btc_id = engine.symbols().id("BTCUSDT");

    // 生成测试数据: 10000 根 1m K线（约 7 天）
    auto btc_bars = generate_test_data(10000, 67000.0);
    printf("Loaded %zu bars for BTCUSDT\n", btc_bars.size());

    // 添加 DataFeed
    engine.add_feed(std::make_unique<CsvFeed>(btc_id, std::move(btc_bars)));

    // 设置 SimBroker
    SimBrokerConfig broker_cfg;
    broker_cfg.cash = 10000.0;
    broker_cfg.commission_rate = 0.0004;
    broker_cfg.slippage = 0.0001;
    engine.set_broker(std::make_unique<SimBroker>(broker_cfg));

    // 添加策略: 双均线 SMA(5) / SMA(20)
    engine.add_strategy(std::make_shared<DualMA>("BTCUSDT", 5, 20));

    // 运行回测
    printf("Running backtest...\n");
    auto result = engine.run();

    // 输出结果
    printf("\n=== Results ===\n");
    printf("Initial cash:   $%.2f\n", result.initial_cash);
    printf("Final equity:   $%.2f\n", result.final_equity);
    printf("Total return:   %.2f%%\n", result.total_return * 100);
    printf("Annual return:  %.2f%%\n", result.annual_return * 100);
    printf("Sharpe ratio:   %.4f\n", result.sharpe);
    printf("Max drawdown:   %.2f%%\n", result.max_drawdown * 100);
    printf("Total trades:   %u\n", result.total_trades);
    printf("Win rate:       %.2f%%\n", result.win_rate * 100);
    printf("Equity samples: %zu\n", result.equity_curve.size());

    return 0;
}
