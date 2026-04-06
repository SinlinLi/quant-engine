"""RSI 反转策略

- RSI < oversold 阈值（默认 30）→ 买入
- RSI > overbought 阈值（默认 70）→ 平仓
- 用于对拍测试：验证 RSI 指标计算和反转类信号的正确性
"""
import qe


class RSIReversal(qe.Strategy):
    def __init__(self, symbol_name: str,
                 period: int = 14,
                 oversold: float = 30.0,
                 overbought: float = 70.0):
        super().__init__()
        self._symbol_name = symbol_name
        self._period = period
        self._oversold = oversold
        self._overbought = overbought
        self._sid = 0
        self._rsi = None

    def on_init(self, ctx: qe.Context):
        self._sid = ctx.symbol(self._symbol_name)
        self._rsi = ctx.rsi(self._sid, self._period)

    def on_bar(self, ctx: qe.Context, symbol_id: int, bar: qe.Bar):
        if symbol_id != self._sid:
            return
        if not self._rsi.ready():
            return

        rsi_val = self._rsi.value()
        pos = ctx.position(self._sid)

        if rsi_val < self._oversold and pos.quantity == 0.0:
            ctx.buy(self._sid, 0.1)
        elif rsi_val > self._overbought and pos.quantity > 0.0:
            ctx.sell(self._sid, pos.quantity)

    def on_stop(self, ctx: qe.Context):
        pos = ctx.position(self._sid)
        if pos.quantity > 0:
            ctx.sell(self._sid, pos.quantity)
