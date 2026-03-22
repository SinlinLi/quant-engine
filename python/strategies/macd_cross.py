"""MACD 金叉死叉策略

- MACD 柱状图由负转正（金叉）→ 买入
- MACD 柱状图由正转负（死叉）→ 平仓
"""
import qe


class MACDCross(qe.Strategy):
    def __init__(self, symbol_name: str,
                 fast: int = 12, slow: int = 26, signal: int = 9):
        super().__init__()
        self._symbol_name = symbol_name
        self._fast = fast
        self._slow = slow
        self._signal = signal
        self._sid = 0
        self._macd = None
        self._prev_hist = 0.0

    def on_init(self, ctx: qe.Context):
        self._sid = ctx.symbol(self._symbol_name)
        self._macd = ctx.macd(self._sid, self._fast, self._slow, self._signal)

    def on_bar(self, ctx: qe.Context, symbol_id: int, bar: qe.Bar):
        if symbol_id != self._sid:
            return
        if not self._macd.ready():
            self._prev_hist = 0.0
            return

        hist = self._macd.histogram()
        pos = ctx.position(self._sid)

        # 金叉：柱状图从负变正
        if self._prev_hist <= 0 < hist and pos.quantity < 1e-12:
            ctx.buy(self._sid, 0.1)

        # 死叉：柱状图从正变负
        if self._prev_hist >= 0 > hist and pos.quantity > 1e-12:
            ctx.sell(self._sid, pos.quantity)

        self._prev_hist = hist

    def on_stop(self, ctx: qe.Context):
        pos = ctx.position(self._sid)
        if pos.quantity > 0:
            ctx.sell(self._sid, pos.quantity)
