# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Test

```bash
# Build everything (from repo root)
mkdir -p build && cd build && cmake .. && make -j$(nproc)

# Run unit tests (48 tests, custom framework - no gtest)
./build/cpp/qe_test

# Run benchmark
./build/cpp/qe_bench

# Run C++ backtest demo
./build/cpp/qe_backtest
```

The pybind11 module (`qe.cpython-*.so`) is built into `build/cpp/` (or `cpp/build/`). Python scripts expect it on `sys.path` via `QE_BUILD_DIR` env var or the default `../cpp/build` relative path.

```bash
# CLI (requires ClickHouse running on localhost:8123)
cd python
python3 cli.py collect BTCUSDT --start 2024-01-01 --end 2024-01-31
python3 cli.py backtest --strategy dual_ma --symbols BTCUSDT --start 2024-01-01 --end 2024-01-31
python3 cli.py list-data
```

## Architecture

**Three-layer design**: C++ core engine → pybind11 bindings → Python strategy/CLI layer.

### C++ Core (`cpp/`)
- **Engine** (`core/engine.h`): Event-driven backtest loop. Min-heap merges bars across multiple symbols by timestamp. Calls `Strategy::on_bar()` for each bar, updates indicators automatically.
- **Context** (`core/context.h`): Strategy's interface to the engine. Provides order placement (`buy`/`sell`/`buy_limit`/`sell_limit`), position queries, and indicator access via `ctx.indicator<T>(symbol_id, args...)`. Template instantiations are cached by a string key (type+args). Indicators can only be created during `on_init`; after that, `lock_init()` prevents new registrations.
- **Strategy** (`core/strategy.h`): Pure virtual `on_bar()`, with optional `on_init`/`on_stop`/`on_tick`/`on_order` hooks.
- **SimBroker** (`core/sim_broker.h`): Simulated broker with market/limit orders, commission, slippage. Portfolio tracks per-symbol positions with avg entry price and PnL.
- **Indicators** (`indicator/`): SMA, EMA, RSI, MACD, Bollinger. All inherit from `Indicator` base class (virtual `update(double)`, `value()`, `ready()`). Use `RingBuffer` for fixed-size circular storage.
- **Analyzer** (`analyzer/performance.h`): Computes Sharpe, max drawdown, annual return, win rate from equity curve.

### pybind11 Bindings (`cpp/bind/pybind_module.cpp`)
- `PyStrategy` trampoline class enables Python subclasses to override C++ virtual functions.
- C++ template `Context::indicator<T>()` is exposed as named methods: `ctx.sma()`, `ctx.ema()`, `ctx.rsi()`, `ctx.macd()`, `ctx.bollinger()`.
- `Engine.add_feed_bars()` accepts a Python list of `Bar` objects (for ClickHouse pipeline).
- `Engine.set_broker()` takes `SimBrokerConfig` (simplified vs C++ `unique_ptr<Broker>`).

### Python Layer (`python/`)
- **cli.py**: CLI entry point with `collect`, `backtest`, `list-data` subcommands. Strategy loading via `_load_strategy()` name dispatch.
- **data_collector.py**: Binance REST API → ClickHouse pipeline. Paginated fetch with exponential backoff retry.
- **strategies/**: Python strategy implementations (dual_ma, macd_cross, momentum_rotation).

### Data Storage
- ClickHouse database `qe` with tables `qe.bars` (OHLCV data) and `qe.backtest_runs` (results).
- CSV files in `data/` for offline testing (`CsvFeed`).

## Test Framework

Custom minimal test framework in `cpp/test/test_framework.h` — not gtest. Tests use `TEST()` macro and `ASSERT_*` helpers. All test files are `#include`-d into `test_main.cpp` as a single compilation unit.

## Key Constraints

- C++17 required, cmake >= 3.20
- pybind11 v2.13.6 fetched via CMake FetchContent
- Python strategies must inherit `qe.Strategy` and implement `on_bar(self, ctx, symbol_id, bar)`
- Indicator creation is only allowed in `on_init()`; calling `ctx.indicator<T>()` / `ctx.sma()` etc. after init raises an error
- Git commit messages in 简体中文
