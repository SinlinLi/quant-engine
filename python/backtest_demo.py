"""回测示例 — 演示 Python 策略驱动 C++ 引擎

Usage:
    python3 backtest_demo.py

生成合成数据跑双均线策略，验证 Python→C++ 绑定的完整流程。
"""
import sys, os, math

# 把 C++ build 目录加入 Python path（支持 QE_BUILD_DIR 环境变量覆盖）
BUILD_DIR = os.environ.get('QE_BUILD_DIR',
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cpp', 'build'))
sys.path.insert(0, os.path.abspath(BUILD_DIR))

import qe
from strategies.dual_ma import DualMA


def generate_bars(count: int, base_price: float) -> list[qe.Bar]:
    """生成模拟 K 线（与 C++ main.cpp 逻辑一致）"""
    bars = []
    ts = 1672531200000  # 2023-01-01 00:00:00 UTC

    for i in range(count):
        wave = math.sin(i * 0.01) * base_price * 0.05
        trend = i * base_price * 0.000001
        noise = ((i * 7 + 13) % 100 - 50) * base_price * 0.0001
        price = base_price + wave + trend + noise

        hl_range = price * 0.005
        bar = qe.Bar()
        bar.timestamp_ms = ts + i * 60000
        bar.open = price - hl_range * 0.2
        bar.high = price + hl_range
        bar.low = price - hl_range
        bar.close = price
        bar.volume = 100.0 + (i % 50) * 2.0
        bar.quote_volume = bar.volume * price
        bars.append(bar)

    return bars


def main():
    print("=== Python Backtest Demo ===\n")

    engine = qe.Engine()
    btc_id = engine.symbol_id("BTCUSDT")

    # 生成 10000 根 1m K线
    bars = generate_bars(10000, 67000.0)
    print(f"Generated {len(bars)} bars for BTCUSDT")

    engine.add_feed_bars(btc_id, bars)

    # 配置 SimBroker
    cfg = qe.SimBrokerConfig()
    cfg.cash = 10000.0
    cfg.commission_rate = 0.0004
    cfg.slippage = 0.0001
    engine.set_broker(cfg)

    # Python 策略
    strategy = DualMA("BTCUSDT", fast_period=5, slow_period=20)
    engine.add_strategy(strategy)

    print("Running backtest...")
    result = engine.run()

    print(f"\n=== Results ===")
    print(f"Initial cash:   ${result.initial_cash:.2f}")
    print(f"Final equity:   ${result.final_equity:.2f}")
    print(f"Total return:   {result.total_return * 100:.2f}%")
    print(f"Annual return:  {result.annual_return * 100:.2f}%")
    print(f"Sharpe ratio:   {result.sharpe:.4f}")
    print(f"Max drawdown:   {result.max_drawdown * 100:.2f}%")
    print(f"Total trades:   {result.total_trades}")
    print(f"Win rate:       {result.win_rate * 100:.2f}%")
    print(f"Equity samples: {len(result.equity_curve)}")


if __name__ == "__main__":
    main()
