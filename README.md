# quant-engine

> **WIP** — 项目开发中，尚未完成全部功能。

加密货币量化回测引擎。C++ 核心引擎负责高性能回测，Python 策略层（pybind11）支持快速迭代，ClickHouse 存储历史行情与回测结果。

A crypto quantitative backtesting engine. C++ core engine for high-performance backtesting, Python strategy layer (pybind11) for rapid iteration, ClickHouse for market data and backtest result storage.

## 架构 / Architecture

```
┌─────────────────────────────────────────────┐
│              Python Strategy Layer           │
│         (pybind11, override C++ virtual)     │
├─────────────────────────────────────────────┤
│              C++ Core Engine                 │
│  ┌─────────┐ ┌──────────┐ ┌──────────────┐  │
│  │ Engine   │ │ SimBroker│ │  Indicators  │  │
│  │ Context  │ │ Portfolio│ │  SMA/EMA/RSI │  │
│  │ Strategy │ │ Order    │ │  MACD/Boll.  │  │
│  └─────────┘ └──────────┘ └──────────────┘  │
├─────────────────────────────────────────────┤
│  Data Layer: CsvFeed / ClickHouse Pipeline   │
└─────────────────────────────────────────────┘
```

## 特性 / Features

- **事件驱动**：小顶堆多币种时间归并，支持任意数量交易对同时回测
- **高性能**：单核 ~770 万 bars/s（无指标），~170 万 bars/s（7 指标），详见 [Benchmark](#benchmark)
- **C++/Python 混合**：pybind11 导出核心类，Python 子类可直接 override C++ 虚函数
- **内置指标**：SMA、EMA、RSI、MACD、Bollinger Bands，指标自动缓存
- **模拟撮合**：市价单/限价单/止损单/止损限价单、maker/taker 分离费率、滑点、成交量参与率限制
- **绩效分析**：Sharpe、Sortino、Calmar、profit factor、最大回撤、年化收益、胜率、净值曲线
- **数据管道**：Binance REST API → ClickHouse，支持任意时间范围批量采集
- **CLI 入口**：`collect`（数据采集）、`backtest`（回测）、`list-data`（查询数据）

---

- **Event-driven**: Min-heap time-merging across multiple symbols
- **High performance**: ~7.7M bars/s (no indicators), ~1.7M bars/s (7 indicators) on single core. See [Benchmark](#benchmark)
- **C++/Python hybrid**: pybind11 exports core classes; Python subclasses can override C++ virtual functions
- **Built-in indicators**: SMA, EMA, RSI, MACD, Bollinger Bands with automatic caching
- **Simulated broker**: Market/limit/stop-market/stop-limit orders, maker/taker fee split, slippage, volume participation limits
- **Performance analytics**: Sharpe, Sortino, Calmar, profit factor, max drawdown, annual return, win rate, equity curve
- **Data pipeline**: Binance REST API → ClickHouse, batch collection for any time range
- **CLI**: `collect` (data collection), `backtest` (run backtest), `list-data` (query data)

## 构建 / Build

```bash
# 依赖: cmake >= 3.20, g++ >= 11 (C++17), Python 3
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 运行测试
./cpp/qe_test

# 运行 benchmark
./cpp/qe_bench
```

## 使用 / Usage

### CLI

```bash
cd python

# 采集 Binance 1m K线数据到 ClickHouse（需要 ClickHouse 运行在 localhost:8123）
python3 cli.py collect BTCUSDT --start 2024-01-01 --end 2024-01-31
python3 cli.py collect BTCUSDT ETHUSDT --start 2024-01-01 --end 2024-12-31 --interval 1h

# 查看已采集的数据
python3 cli.py list-data

# 用双均线策略回测（默认 1 分钟 K 线）
python3 cli.py backtest --strategy dual_ma --symbols BTCUSDT \
    --start 2024-01-01 --end 2024-01-31

# 指定 K 线周期：1m, 5m, 15m, 1h, 4h, 1d
python3 cli.py backtest --strategy macd_cross --symbols BTCUSDT \
    --start 2024-01-01 --end 2025-01-01 --interval 1d

# 自定义参数和资金
python3 cli.py backtest --strategy dual_ma --symbols BTCUSDT \
    --start 2024-01-01 --end 2025-01-01 --interval 4h \
    --cash 50000 --commission 0.0004 --slippage 0.0001 \
    --params fast=10 slow=30

# 多币种动量轮动
python3 cli.py backtest --strategy momentum_rotation \
    --symbols BTCUSDT ETHUSDT SOLUSDT \
    --start 2024-01-01 --end 2024-06-30 \
    --params lookback=20

# 生成可视化图表（净值曲线 + 回撤 + 价格走势 + 绩效摘要）
python3 cli.py backtest --strategy macd_cross --symbols BTCUSDT \
    --start 2024-01-01 --end 2025-01-01 --interval 1d --plot

# 指定图表输出路径
python3 cli.py backtest --strategy dual_ma --symbols BTCUSDT \
    --start 2024-01-01 --end 2025-01-01 --interval 1h \
    --plot -o result.png
```

**内置策略**：

| 策略 | 说明 | 参数 |
|------|------|------|
| `dual_ma` | 双均线交叉 | `fast`(5), `slow`(20) |
| `macd_cross` | MACD 信号交叉 | `fast`(12), `slow`(26), `signal`(9) |
| `momentum_rotation` | 多币种动量轮动 | `lookback`(20) |

**K 线周期**：非 1m 周期从 `klines_1m` 实时聚合（`argMin(open)`/`argMax(close)` 保证 OHLC 正确）。

### C++ 示例

```cpp
#include "core/engine.h"
#include "core/sim_broker.h"
#include "data/csv_feed.h"
#include "indicator/sma.h"

class MyStrategy : public qe::Strategy {
    void on_init(qe::Context& ctx) override {
        auto sid = ctx.symbol("BTCUSDT");
        ctx.indicator<qe::SMA>(sid, 10);
        ctx.indicator<qe::SMA>(sid, 30);
    }
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar& bar) override {
        auto& fast = ctx.indicator<qe::SMA>(sid, 10);
        auto& slow = ctx.indicator<qe::SMA>(sid, 30);
        if (!fast.ready() || !slow.ready()) return;
        if (fast.value() > slow.value() && ctx.position(sid).quantity < 1e-12)
            ctx.buy(sid, 0.01);
        else if (fast.value() < slow.value() && ctx.position(sid).quantity > 1e-12)
            ctx.sell(sid, ctx.position(sid).quantity);
    }
};

int main() {
    qe::Engine engine;
    auto btc = engine.symbols().id("BTCUSDT");
    engine.add_feed(std::make_unique<qe::CsvFeed>(btc, "data/btcusdt_1m.csv"));

    qe::SimBrokerConfig cfg{.cash = 10000.0, .commission_rate = 0.0004};
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));
    engine.add_strategy(std::make_shared<MyStrategy>());

    auto result = engine.run();
    printf("Sharpe: %.2f, Return: %.2f%%\n", result.sharpe, result.total_return * 100);
}
```

### Python 示例

```python
import qe

class DualMA(qe.Strategy):
    def on_init(self, ctx):
        sid = ctx.symbol("BTCUSDT")
        ctx.sma(sid, 10)
        ctx.sma(sid, 30)

    def on_bar(self, ctx, symbol_id, bar):
        fast = ctx.sma(symbol_id, 10)
        slow = ctx.sma(symbol_id, 30)
        if not fast.ready() or not slow.ready():
            return
        pos = ctx.position(symbol_id).quantity
        if fast.value() > slow.value() and pos < 1e-12:
            ctx.buy(symbol_id, 0.01)
        elif fast.value() < slow.value() and pos > 1e-12:
            ctx.sell(symbol_id, pos)

engine = qe.Engine()
btc = engine.symbol_id("BTCUSDT")
engine.add_feed_bars(btc, bars)  # bars: list[qe.Bar]

config = qe.SimBrokerConfig()
config.cash = 10000.0
config.commission_rate = 0.0004
config.slippage = 0.0001
engine.set_broker(config)
engine.add_strategy(DualMA())

result = engine.run()
print(f"Sharpe: {result.sharpe:.2f}, Return: {result.total_return:.2%}")
print(f"Sortino: {result.sortino:.2f}, Calmar: {result.calmar:.2f}")
print(f"Max Drawdown: {result.max_drawdown:.2%}, Win Rate: {result.win_rate:.2%}")
```

## Benchmark

测试环境：Intel i9-13900 (单核)，`-O2`，GCC 13.3

```
[1] Throughput scaling (BuyAndHold, 1 symbol)
  10K bars      1.9ms     5.4M bars/s
  100K bars    13.5ms     7.4M bars/s
  1M bars       129ms     7.7M bars/s

[2] Multi-symbol scaling (DualSMA 10/30, 100K bars)
  1 symbol      20ms     5.1M bars/s
  4 symbols     87ms     4.6M bars/s
  16 symbols   378ms     4.2M bars/s
  64 symbols   1.88s     3.4M bars/s

[3] Indicator overhead (7 indicators, 1 symbol)
  10K bars      6.1ms    1.6M bars/s
  100K bars      58ms    1.7M bars/s
  500K bars     289ms    1.7M bars/s

[4] Stress test (7 indicators, 100K bars, multi-symbol)
  1 symbol       61ms    1.7M bars/s
  4 symbols     250ms    1.6M bars/s
  16 symbols    1.05s    1.5M bars/s
```

## 项目结构 / Project Structure

```
quant-engine/
├── cpp/
│   ├── core/           # Engine, Context, Broker, Strategy, SymbolTable, Portfolio, Order
│   ├── data/           # Bar, DataFeed, CsvFeed
│   ├── indicator/      # SMA, EMA, RSI, MACD, Bollinger
│   ├── analyzer/       # Performance metrics (Sharpe, Sortino, Calmar, etc.)
│   ├── strategies/     # Example: DualMA
│   ├── bind/           # pybind11 module
│   ├── bench/          # Benchmark
│   └── test/           # Unit tests
├── python/
│   ├── cli.py              # CLI entry point
│   ├── data_collector.py   # Binance → ClickHouse pipeline
│   └── strategies/         # Python strategy examples
├── data/               # CSV test data
└── CMakeLists.txt
```

## 进度 / Status

- [x] Phase 1: C++ 回测引擎核心（Engine/Context/Strategy/Broker/Feed/Indicators）
- [x] Phase 2.1: Python 策略层（pybind11）+ ClickHouse 数据管道 + CLI
- [x] Phase 2.2: 现货回测可用（止损单、maker/taker 费率、成交量限制、Sortino/Calmar 等绩效指标）
- [ ] Phase 3: 实盘交易（Binance WebSocket + LiveBroker + 风控）
- [ ] Phase 4: 可视化 + 文档

## License

MIT
