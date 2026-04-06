"""布林带突破策略

- 价格 > 上轨 → 买入（趋势突破）
- 价格 < 中轨（SMA）→ 平仓
- 用于对拍测试：验证布林带指标和突破类信号
"""
import qe


class BollingerBreakout(qe.Strategy):
    def __init__(self, symbol_name: str,
                 period: int = 20,
                 num_std: float = 2.0):
        super().__init__()
        self._symbol_name = symbol_name
        self._period = period
        self._num_std = num_std
        self._sid = 0
        self._bb = None

    def on_init(self, ctx: qe.Context):
        self._sid = ctx.symbol(self._symbol_name)
        self._bb = ctx.bollinger(self._sid, self._period, self._num_std)

    def on_bar(self, ctx: qe.Context, symbol_id: int, bar: qe.Bar):
        if symbol_id != self._sid:
            return
        if not self._bb.ready():
            return

        upper = self._bb.upper()
        middle = self._bb.value()  # value() 返回中轨 (SMA)
        pos = ctx.position(self._sid)

        if bar.close > upper and pos.quantity == 0.0:
            ctx.buy(self._sid, 0.1)
        elif bar.close < middle and pos.quantity > 0.0:
            ctx.sell(self._sid, pos.quantity)

    def on_stop(self, ctx: qe.Context):
        pos = ctx.position(self._sid)
        if pos.quantity > 0:
            ctx.sell(self._sid, pos.quantity)
