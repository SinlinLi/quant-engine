"""多品种动量轮动策略

每根 bar 比较所有品种的短期涨幅（close / SMA - 1），
持有动量最强的品种，其余平仓。

用于测试引擎的小顶堆多品种时间归并功能。
"""
import qe


class MomentumRotation(qe.Strategy):
    def __init__(self, symbol_names: list[str], lookback: int = 20):
        super().__init__()
        self._symbol_names = symbol_names
        self._lookback = lookback
        self._sids = []
        self._smas = {}          # sid -> SMA ref
        self._last_close = {}    # sid -> latest close
        self._bars_seen = set()  # sids seen in current timestamp
        self._current_ts = 0

    def on_init(self, ctx: qe.Context):
        for name in self._symbol_names:
            sid = ctx.symbol(name)
            self._sids.append(sid)
            self._smas[sid] = ctx.sma(sid, self._lookback)
            self._last_close[sid] = 0.0

    def on_bar(self, ctx: qe.Context, symbol_id: int, bar: qe.Bar):
        if symbol_id not in self._smas:
            return

        self._last_close[symbol_id] = bar.close

        # 跟踪当前时间戳，等所有品种都到了同一时刻再决策
        if bar.timestamp_ms != self._current_ts:
            self._bars_seen.clear()
            self._current_ts = bar.timestamp_ms
        self._bars_seen.add(symbol_id)

        # 只有当所有品种都收到了当前时刻的 bar 才做决策
        if len(self._bars_seen) < len(self._sids):
            return

        # 检查所有 SMA 是否就绪
        if not all(self._smas[sid].ready() for sid in self._sids):
            return

        # 计算各品种动量: close / SMA - 1
        momentum = {}
        for sid in self._sids:
            sma_val = self._smas[sid].value()
            if sma_val > 0:
                momentum[sid] = self._last_close[sid] / sma_val - 1.0

        if not momentum:
            return

        # 选出最强品种
        best_sid = max(momentum, key=momentum.get)

        # 平掉非最强品种的仓位
        for sid in self._sids:
            pos = ctx.position(sid)
            if sid != best_sid and pos.quantity > 1e-12:
                ctx.sell(sid, pos.quantity)

        # 买入最强品种（如果还没持有）
        best_pos = ctx.position(best_sid)
        if best_pos.quantity < 1e-12:
            # 用当前可用资金的 95% 买入
            price = self._last_close[best_sid]
            if price > 0:
                qty = (ctx.cash() * 0.95) / price
                if qty > 1e-12:
                    ctx.buy(best_sid, qty)

    def on_stop(self, ctx: qe.Context):
        for sid in self._sids:
            pos = ctx.position(sid)
            if pos.quantity > 1e-12:
                ctx.sell(sid, pos.quantity)
