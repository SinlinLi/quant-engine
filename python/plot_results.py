"""回测结果可视化

生成四面板图表：净值曲线 + 回撤 + 价格走势 + 绩效摘要。
"""
import os
from datetime import datetime, timezone

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import matplotlib.ticker as mticker


def _ts_to_dt(ms: int) -> datetime:
    return datetime.fromtimestamp(ms / 1000, tz=timezone.utc)


def _downsample(xs, ys, max_points=4000):
    """等间隔抽样，保留首尾"""
    n = len(xs)
    if n <= max_points:
        return xs, ys
    step = n / max_points
    indices = [int(i * step) for i in range(max_points)]
    if indices[-1] != n - 1:
        indices.append(n - 1)
    return [xs[i] for i in indices], [ys[i] for i in indices]


def _calc_drawdown(equity_curve):
    """从净值序列计算回撤百分比"""
    peak = equity_curve[0]
    dd = []
    for eq in equity_curve:
        if eq > peak:
            peak = eq
        dd.append((eq - peak) / peak if peak > 0 else 0.0)
    return dd


def plot_backtest(result, bars_by_symbol, strategy_name, params, output_path):
    """绘制回测结果图表

    Args:
        result: qe.PerformanceResult
        bars_by_symbol: dict[str, list[qe.Bar]] — 每个 symbol 的 bar 列表
        strategy_name: 策略名称
        params: 策略参数 dict
        output_path: 输出图片路径
    """
    equity = list(result.equity_curve)
    if not equity:
        print("Warning: empty equity curve, skipping plot")
        return

    # 合并所有 symbol 的时间戳并排序（与 engine 的 min-heap 归并一致）
    all_ts = []
    for bars in bars_by_symbol.values():
        all_ts.extend(b.timestamp_ms for b in bars)
    all_ts.sort()

    # equity_curve 长度 = bars 数量 + 1（最后的 flush 采样点）
    # 时间轴：用 bar 的时间戳，最后一个点复用最末时间戳
    if len(all_ts) >= len(equity):
        times = [_ts_to_dt(ts) for ts in all_ts[:len(equity)]]
    else:
        times = [_ts_to_dt(ts) for ts in all_ts]
        # 补齐：最后一个点用最末时间
        while len(times) < len(equity):
            times.append(times[-1] if times else _ts_to_dt(0))

    drawdown = _calc_drawdown(equity)

    # 构建价格序列（按时间归并，只取第一个 symbol 用于价格面板）
    primary_symbol = list(bars_by_symbol.keys())[0]
    primary_bars = bars_by_symbol[primary_symbol]
    price_times = [_ts_to_dt(b.timestamp_ms) for b in primary_bars]
    price_close = [b.close for b in primary_bars]

    # 抽样避免渲染过慢
    times_eq, equity_ds = _downsample(times, equity)
    _, dd_ds = _downsample(times, drawdown)
    price_times_ds, price_ds = _downsample(price_times, price_close)

    # ── 绘图 ──
    fig = plt.figure(figsize=(16, 12), facecolor="white")
    fig.subplots_adjust(hspace=0.35, left=0.08, right=0.72, top=0.92, bottom=0.06)

    # 标题
    param_str = ", ".join(f"{k}={v}" for k, v in params.items()) if params else "default"
    symbols_str = ", ".join(bars_by_symbol.keys())
    fig.suptitle(
        f"{strategy_name}  |  {symbols_str}  |  {param_str}",
        fontsize=14, fontweight="bold", y=0.97,
    )

    date_fmt = mdates.DateFormatter("%m-%d") if (times[-1] - times[0]).days < 90 \
        else mdates.DateFormatter("%Y-%m")

    # ── Panel 1: 净值曲线 ──
    ax1 = fig.add_subplot(3, 1, 1)
    ax1.plot(times_eq, equity_ds, color="#0d9488", linewidth=1.2, label="Equity")
    ax1.axhline(result.initial_cash, color="#94a3b8", linestyle="--", linewidth=0.8, label="Initial")
    ax1.fill_between(times_eq, result.initial_cash, equity_ds,
                     where=[e >= result.initial_cash for e in equity_ds],
                     color="#0d9488", alpha=0.1)
    ax1.fill_between(times_eq, result.initial_cash, equity_ds,
                     where=[e < result.initial_cash for e in equity_ds],
                     color="#ef4444", alpha=0.1)
    ax1.set_ylabel("Equity ($)")
    ax1.set_title("Equity Curve", fontsize=11, loc="left")
    ax1.legend(loc="upper left", fontsize=9)
    ax1.xaxis.set_major_formatter(date_fmt)
    ax1.yaxis.set_major_formatter(mticker.FormatStrFormatter("$%.0f"))
    ax1.grid(True, alpha=0.3)

    # ── Panel 2: 回撤 ──
    ax2 = fig.add_subplot(3, 1, 2)
    ax2.fill_between(times_eq, 0, [d * 100 for d in dd_ds], color="#ef4444", alpha=0.4)
    ax2.plot(times_eq, [d * 100 for d in dd_ds], color="#dc2626", linewidth=0.8)
    ax2.set_ylabel("Drawdown (%)")
    ax2.set_title("Drawdown", fontsize=11, loc="left")
    ax2.xaxis.set_major_formatter(date_fmt)
    ax2.yaxis.set_major_formatter(mticker.FormatStrFormatter("%.1f%%"))
    ax2.grid(True, alpha=0.3)

    # ── Panel 3: 价格走势 ──
    ax3 = fig.add_subplot(3, 1, 3)
    ax3.plot(price_times_ds, price_ds, color="#6366f1", linewidth=0.9, label=primary_symbol)
    ax3.set_ylabel("Price ($)")
    ax3.set_xlabel("Date")
    ax3.set_title(f"{primary_symbol} Price", fontsize=11, loc="left")
    ax3.legend(loc="upper left", fontsize=9)
    ax3.xaxis.set_major_formatter(date_fmt)
    ax3.grid(True, alpha=0.3)

    # ── 右侧绩效摘要 ──
    stats_ax = fig.add_axes([0.75, 0.15, 0.23, 0.75])
    stats_ax.axis("off")

    def _safe_attr(obj, attr, fmt=".3f"):
        try:
            v = getattr(obj, attr)
            return f"{v:{fmt}}"
        except AttributeError:
            return "N/A"

    stats = [
        ("Performance Summary", None),
        ("", None),
        ("Initial Cash", f"${result.initial_cash:,.2f}"),
        ("Final Equity", f"${result.final_equity:,.2f}"),
        ("", None),
        ("Total Return", f"{result.total_return * 100:+.2f}%"),
        ("Annual Return", f"{result.annual_return * 100:+.2f}%"),
        ("Max Drawdown", f"{result.max_drawdown * 100:.2f}%"),
        ("", None),
        ("Sharpe", _safe_attr(result, "sharpe")),
        ("Sortino", _safe_attr(result, "sortino")),
        ("Calmar", _safe_attr(result, "calmar")),
        ("Profit Factor", _safe_attr(result, "profit_factor")),
        ("", None),
        ("Total Trades", f"{result.total_trades}"),
        ("Win Rate", f"{result.win_rate * 100:.1f}%"),
    ]

    y = 0.98
    for label, value in stats:
        if value is None and label:
            # 标题行
            stats_ax.text(0.0, y, label, fontsize=12, fontweight="bold",
                         transform=stats_ax.transAxes, va="top")
            y -= 0.05
        elif value is None:
            y -= 0.02
        else:
            # 根据值着色
            color = "#111827"
            if "Return" in label:
                color = "#059669" if value.startswith("+") else "#dc2626"
            elif label == "Sharpe" or label == "Sortino" or label == "Calmar":
                try:
                    v = float(value)
                    color = "#059669" if v > 0 else "#dc2626"
                except ValueError:
                    pass

            stats_ax.text(0.0, y, label, fontsize=10, color="#6b7280",
                         transform=stats_ax.transAxes, va="top")
            stats_ax.text(1.0, y, value, fontsize=10, fontweight="bold",
                         color=color, ha="right",
                         transform=stats_ax.transAxes, va="top")
            y -= 0.045

    # 保存
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"\nChart saved to: {output_path}")
