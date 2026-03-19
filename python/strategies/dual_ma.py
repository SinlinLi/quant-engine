"""双均线策略 — Python 版

演示 Python 策略通过 pybind11 调用 C++ 引擎。
策略逻辑与 C++ 版 DualMA 完全一致：
  - fast SMA 上穿 slow SMA → 买入
  - fast SMA 下穿 slow SMA → 平仓
"""
import qe


class DualMA(qe.Strategy):
    def __init__(self, symbol_name: str, fast_period: int = 5, slow_period: int = 20):
        super().__init__()
        self._symbol_name = symbol_name
        self._fast_period = fast_period
        self._slow_period = slow_period
        self._sid = 0
        self._fast = None
        self._slow = None

    def on_init(self, ctx: qe.Context):
        self._sid = ctx.symbol(self._symbol_name)
        self._fast = ctx.sma(self._sid, self._fast_period)
        self._slow = ctx.sma(self._sid, self._slow_period)

    def on_bar(self, ctx: qe.Context, symbol_id: int, bar: qe.Bar):
        if symbol_id != self._sid:
            return
        if not self._fast.ready() or not self._slow.ready():
            return

        pos = ctx.position(self._sid)
        if self._fast.value() > self._slow.value() and pos.quantity == 0.0:
            ctx.buy(self._sid, 0.1)
        elif self._fast.value() < self._slow.value() and pos.quantity > 0.0:
            ctx.sell(self._sid, pos.quantity)

    def on_stop(self, ctx: qe.Context):
        pos = ctx.position(self._sid)
        if pos.quantity > 0:
            ctx.sell(self._sid, pos.quantity)
