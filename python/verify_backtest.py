#!/usr/bin/env python3
"""纯 Python 独立回测验证器

不依赖 C++ 引擎，从零实现策略逻辑和撮合，
对比 C++ 引擎的结果以验证正确性。
"""
import sys, os
BUILD_DIR = os.environ.get("QE_BUILD_DIR",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cpp", "build"))
sys.path.insert(0, os.path.abspath(BUILD_DIR))
import clickhouse_connect
import qe


# ─── 纯 Python 指标 ───

class PySMA:
    def __init__(self, period):
        self.period = period
        self.buf = []

    def update(self, close):
        self.buf.append(close)
        if len(self.buf) > self.period:
            self.buf.pop(0)

    def ready(self):
        return len(self.buf) >= self.period

    def value(self):
        return sum(self.buf) / len(self.buf)


class PyEMA:
    def __init__(self, period):
        self.period = period
        self.k = 2.0 / (period + 1)
        self._val = 0.0
        self._count = 0

    def update(self, close):
        self._count += 1
        if self._count == 1:
            self._val = close
        elif self._count <= self.period:
            # SMA for warmup
            self._val = self._val + (close - self._val) / self._count
        else:
            self._val = close * self.k + self._val * (1 - self.k)

    def ready(self):
        return self._count >= self.period

    def value(self):
        return self._val


class PyMACD:
    def __init__(self, fast=12, slow=26, signal=9):
        self.fast_ema = PyEMA(fast)
        self.slow_ema = PyEMA(slow)
        self.signal_ema = PyEMA(signal)
        self._macd = 0.0
        self._count = 0
        self.slow_period = slow

    def update(self, close):
        self.fast_ema.update(close)
        self.slow_ema.update(close)
        self._count += 1
        if self.slow_ema.ready():
            self._macd = self.fast_ema.value() - self.slow_ema.value()
            self.signal_ema.update(self._macd)

    def ready(self):
        return self.slow_ema.ready() and self.signal_ema.ready()

    def histogram(self):
        return self._macd - self.signal_ema.value()


# ─── 纯 Python 撮合器 ───

class PyBroker:
    def __init__(self, cash=10000.0, commission_rate=0.0004):
        self.cash = cash
        self.commission_rate = commission_rate
        self.position = 0.0       # quantity
        self.avg_price = 0.0
        self.trades = 0           # closed trades (round trips)
        self.wins = 0
        self.realized_pnl = 0.0
        self.last_price = 0.0

    def buy(self, price, qty):
        cost = price * qty
        comm = cost * self.commission_rate
        if self.cash < cost + comm:
            return False
        self.cash -= cost + comm
        if self.position < 1e-12:
            self.avg_price = price
        else:
            self.avg_price = (self.avg_price * self.position + price * qty) / (self.position + qty)
        self.position += qty
        return True

    def sell(self, price, qty):
        if qty > self.position + 1e-12:
            return False
        revenue = price * qty
        comm = revenue * self.commission_rate
        pnl = (price - self.avg_price) * qty  # 与 C++ 一致：pnl 不含佣金
        self.realized_pnl += pnl
        self.cash += revenue - comm
        self.position -= qty
        self.trades += 1
        if pnl > 0:
            self.wins += 1
        return True

    def equity(self, current_price):
        return self.cash + self.position * current_price


# ─── 加载数据 ───

def load_bars(symbol, start, end):
    import os
    client = clickhouse_connect.get_client(
        host="localhost", port=8123,
        username=os.environ.get("QE_CH_USER", "default"),
        password=os.environ.get("QE_CH_PASSWORD", ""),
    )
    result = client.query(
        "SELECT toInt64(toUnixTimestamp64Milli(open_time)), "
        "open, high, low, close, volume, quote_volume "
        "FROM qe.klines_1m "
        "WHERE symbol = %(symbol)s "
        "AND open_time >= %(start)s AND open_time < %(end)s "
        "ORDER BY open_time",
        parameters={"symbol": symbol, "start": start, "end": end},
    )
    return result.result_rows


# ─── 验证双均线策略 ───

def verify_dual_ma(bars, fast_p=5, slow_p=20, cash=10000.0):
    broker = PyBroker(cash=cash)
    fast = PySMA(fast_p)
    slow = PySMA(slow_p)

    for row in bars:
        ts, open_, high, low, close, vol, qvol = row
        # 市价单按 open 成交（下一根 bar 的 open，但单 bar 模拟中用当前 bar 的 open）
        # C++ 引擎: on_bar 中 buy/sell 提交市价单，下一次 broker.on_bar 时按 bar.open 成交
        # 但在纯 Python 里我们先更新指标再决策，订单在下一根 bar 的 open 成交
        fast.update(close)
        slow.update(close)
        broker.last_price = close

    # 重新跑，这次模拟延迟成交
    broker = PyBroker(cash=cash)
    fast = PySMA(fast_p)
    slow = PySMA(slow_p)
    pending_action = None  # ("buy", qty) or ("sell", qty)

    for row in bars:
        ts, open_, high, low, close, vol, qvol = row

        # 先成交挂单（按 open 价格）
        if pending_action:
            act, qty = pending_action
            if act == "buy":
                broker.buy(open_, qty)
            elif act == "sell":
                broker.sell(open_, qty)
            pending_action = None

        # 更新指标
        fast.update(close)
        slow.update(close)
        broker.last_price = close

        if not fast.ready() or not slow.ready():
            continue

        # 信号
        pos = broker.position
        if fast.value() > slow.value() and pos < 1e-12:
            pending_action = ("buy", 0.1)
        elif fast.value() < slow.value() and pos > 1e-12:
            pending_action = ("sell", pos)

    # on_stop: 清仓
    if broker.position > 1e-12:
        broker.sell(bars[-1][4], broker.position)  # close price of last bar

    return broker


# ─── 验证 MACD 金叉死叉策略 ───

def verify_macd_cross(bars, fast=12, slow=26, signal=9, cash=10000.0):
    broker = PyBroker(cash=cash)
    macd = PyMACD(fast, slow, signal)
    prev_hist = 0.0
    pending_action = None

    for row in bars:
        ts, open_, high, low, close, vol, qvol = row

        # 先成交挂单
        if pending_action:
            act, qty = pending_action
            if act == "buy":
                broker.buy(open_, qty)
            elif act == "sell":
                broker.sell(open_, qty)
            pending_action = None

        # 更新指标
        macd.update(close)
        broker.last_price = close

        if not macd.ready():
            prev_hist = 0.0
            continue

        hist = macd.histogram()
        pos = broker.position

        # 金叉
        if prev_hist <= 0 < hist and pos < 1e-12:
            pending_action = ("buy", 0.1)
        # 死叉
        if prev_hist >= 0 > hist and pos > 1e-12:
            pending_action = ("sell", pos)

        prev_hist = hist

    # on_stop: 清仓
    if broker.position > 1e-12:
        broker.sell(bars[-1][4], broker.position)

    return broker


# ─── C++ 引擎回测 ───

def run_cpp_dual_ma(bars_raw, symbol, fast_p=5, slow_p=20, cash=10000.0):
    from strategies.dual_ma import DualMA
    engine = qe.Engine()
    sid = engine.symbol_id(symbol)

    bars = []
    for row in bars_raw:
        bar = qe.Bar()
        bar.timestamp_ms, bar.open, bar.high, bar.low, bar.close, bar.volume, bar.quote_volume = row
        bars.append(bar)
    engine.add_feed_bars(sid, bars)

    cfg = qe.SimBrokerConfig()
    cfg.cash = cash
    cfg.commission_rate = 0.0004
    cfg.slippage = 0.0
    engine.set_broker(cfg)
    strategy = DualMA(symbol, fast_period=fast_p, slow_period=slow_p)
    engine.add_strategy(strategy)
    return engine.run()


def run_cpp_macd(bars_raw, symbol, fast=12, slow=26, signal=9, cash=10000.0):
    from strategies.macd_cross import MACDCross
    engine = qe.Engine()
    sid = engine.symbol_id(symbol)

    bars = []
    for row in bars_raw:
        bar = qe.Bar()
        bar.timestamp_ms, bar.open, bar.high, bar.low, bar.close, bar.volume, bar.quote_volume = row
        bars.append(bar)
    engine.add_feed_bars(sid, bars)

    cfg = qe.SimBrokerConfig()
    cfg.cash = cash
    cfg.commission_rate = 0.0004
    cfg.slippage = 0.0
    engine.set_broker(cfg)
    strategy = MACDCross(symbol, fast=fast, slow=slow, signal=signal)
    engine.add_strategy(strategy)
    return engine.run()


# ─── 对比 ───

def compare(name, py_broker, cpp_result, last_close):
    py_equity = py_broker.equity(last_close)
    cpp_equity = cpp_result.final_equity
    equity_diff = abs(py_equity - cpp_equity)
    equity_pct = equity_diff / cpp_equity * 100 if cpp_equity else 0

    trade_match = py_broker.trades == cpp_result.total_trades
    py_wr = (py_broker.wins / py_broker.trades * 100) if py_broker.trades else 0
    cpp_wr = cpp_result.win_rate * 100

    print(f"\n{'='*60}")
    print(f"  {name}")
    print(f"{'='*60}")
    print(f"  {'':30s} {'Python':>12s} {'C++ Engine':>12s} {'Match':>8s}")
    print(f"  {'─'*62}")
    print(f"  {'Final equity':30s} {py_equity:>12.2f} {cpp_equity:>12.2f} {'✓' if equity_pct < 0.01 else '✗ %.4f%%' % equity_pct:>8s}")
    print(f"  {'Total trades':30s} {py_broker.trades:>12d} {cpp_result.total_trades:>12d} {'✓' if trade_match else '✗':>8s}")
    print(f"  {'Win rate':30s} {py_wr:>11.2f}% {cpp_wr:>11.2f}% {'✓' if abs(py_wr - cpp_wr) < 0.1 else '✗':>8s}")
    print(f"  {'Total return':30s} {(py_equity/10000-1)*100:>11.2f}% {cpp_result.total_return*100:>11.2f}%")

    ok = equity_pct < 0.01 and trade_match
    print(f"\n  Result: {'PASS ✓' if ok else 'FAIL ✗'}")
    return ok


def main():
    print("Loading BTCUSDT 1h data (2024) ...")
    bars = load_bars("BTCUSDT", "2024-01-01", "2025-01-01")
    print(f"  {len(bars)} bars loaded\n")
    last_close = bars[-1][4]

    all_pass = True

    # DualMA(5, 20)
    print("Running DualMA(5/20) ...")
    py = verify_dual_ma(bars, fast_p=5, slow_p=20)
    cpp = run_cpp_dual_ma(bars, "BTCUSDT", fast_p=5, slow_p=20)
    all_pass &= compare("DualMA(5/20) on BTCUSDT 1h", py, cpp, last_close)

    # MACD Cross(12/26/9)
    print("\nRunning MACD Cross(12/26/9) ...")
    py = verify_macd_cross(bars, fast=12, slow=26, signal=9)
    cpp = run_cpp_macd(bars, "BTCUSDT", fast=12, slow=26, signal=9)
    all_pass &= compare("MACD Cross(12/26/9) on BTCUSDT 1h", py, cpp, last_close)

    print(f"\n{'='*60}")
    if all_pass:
        print("  ALL VERIFICATION PASSED ✓")
    else:
        print("  SOME VERIFICATION FAILED ✗")
    print(f"{'='*60}")

    return 0 if all_pass else 1


if __name__ == "__main__":
    sys.exit(main())
