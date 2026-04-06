#!/usr/bin/env python3
"""quant-engine vs freqtrade 对拍脚本

从 ClickHouse 加载相同数据，分别用 qe 引擎和 freqtrade 回测，对比结果。
freqtrade 运行在隔离 venv (.ft_env) 中，通过 subprocess 调用。

Usage:
    # 单策略对拍
    python3 freqtrade_compare.py --strategy dual_ma --symbol BTCUSDT \
        --start 2024-01-01 --end 2024-06-30 --interval 1h

    # 批量对拍（全部策略 × 多场景）
    python3 freqtrade_compare.py --batch

    # 带图表
    python3 freqtrade_compare.py --strategy rsi_reversal --symbol ETHUSDT \
        --start 2024-01-01 --end 2024-06-30 --interval 1h --plot
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path

# ── qe engine ──────────────────────────────────────────────────────────
BUILD_DIR = os.environ.get(
    "QE_BUILD_DIR",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cpp", "build"),
)
sys.path.insert(0, os.path.abspath(BUILD_DIR))

import clickhouse_connect

SCRIPT_DIR = Path(__file__).resolve().parent
FT_VENV = SCRIPT_DIR / ".ft_env"
FT_PYTHON = FT_VENV / "bin" / "python3"
FT_STRATEGIES_DIR = SCRIPT_DIR / "ft_strategies"

# ── ClickHouse 连接参数 ────────────────────────────────────────────────
CH_HOST = os.environ.get("QE_CH_HOST", "localhost")
CH_PORT = int(os.environ.get("QE_CH_PORT", "8123"))
CH_USER = os.environ.get("QE_CH_USER", "default")
CH_PASSWORD = os.environ.get("QE_CH_PASSWORD", "")

# ── 策略映射 ───────────────────────────────────────────────────────────
STRATEGY_MAP = {
    "dual_ma":           "FtDualMA",
    "macd_cross":        "FtMACDCross",
    "rsi_reversal":      "FtRSIReversal",
    "bollinger_breakout": "FtBollingerBreakout",
}

# ── 批量测试场景 ───────────────────────────────────────────────────────
BATCH_SCENARIOS = [
    # (strategy, symbol, start, end, interval)
    ("dual_ma",           "BTCUSDT", "2024-01-01", "2024-06-30", "1h"),
    ("dual_ma",           "ETHUSDT", "2024-03-01", "2024-06-30", "1h"),
    ("dual_ma",           "BTCUSDT", "2024-01-01", "2025-01-01", "1d"),
    ("macd_cross",        "BTCUSDT", "2024-01-01", "2025-01-01", "1d"),
    ("macd_cross",        "BTCUSDT", "2024-01-01", "2024-06-30", "1h"),
    ("rsi_reversal",      "BTCUSDT", "2024-01-01", "2024-06-30", "1h"),
    ("rsi_reversal",      "ETHUSDT", "2024-01-01", "2024-06-30", "1h"),
    ("bollinger_breakout", "BTCUSDT", "2024-01-01", "2024-06-30", "1h"),
    ("bollinger_breakout", "ETHUSDT", "2024-01-01", "2024-06-30", "1h"),
    ("dual_ma",           "SOLUSDT", "2024-01-01", "2024-06-30", "1h"),
]

# ── 时间聚合 ───────────────────────────────────────────────────────────
_INTERVAL_TO_CH = {
    "1m": None,
    "5m": "toStartOfFiveMinutes",
    "15m": "toStartOfFifteenMinutes",
    "1h": "toStartOfHour",
    "4h": "toStartOfInterval({col}, INTERVAL 4 HOUR)",
    "1d": "toStartOfDay",
}


def load_ohlcv_from_ch(client, symbol: str, start: str, end: str,
                        interval: str = "1h"):
    """从 ClickHouse 加载 OHLCV，返回 list of [ts_ms, o, h, l, c, v]"""
    if interval not in _INTERVAL_TO_CH:
        raise ValueError(f"unsupported interval: {interval}")

    if interval == "1m":
        query = (
            "SELECT toInt64(toUnixTimestamp64Milli(open_time)), "
            "open, high, low, close, volume "
            "FROM qe.klines_1m "
            "WHERE symbol = %(symbol)s "
            "AND open_time >= %(start)s AND open_time < %(end)s "
            "ORDER BY open_time"
        )
    else:
        fn = _INTERVAL_TO_CH[interval]
        if "{col}" in fn:
            bucket = fn.format(col="open_time")
        else:
            bucket = f"{fn}(open_time)"
        query = (
            f"SELECT toInt64(toUnixTimestamp64Milli(toDateTime64(bucket, 3, 'UTC'))), "
            f"argMin(open, open_time), max(high), min(low), "
            f"argMax(close, open_time), sum(volume) "
            f"FROM ("
            f"  SELECT {bucket} AS bucket, open_time, open, high, low, close, volume "
            f"  FROM qe.klines_1m "
            f"  WHERE symbol = %(symbol)s "
            f"  AND open_time >= %(start)s AND open_time < %(end)s"
            f") GROUP BY bucket ORDER BY bucket"
        )

    result = client.query(query, parameters={"symbol": symbol, "start": start, "end": end})
    rows = []
    for r in result.result_rows:
        rows.append([int(r[0]), float(r[1]), float(r[2]), float(r[3]),
                      float(r[4]), float(r[5])])
    return rows


# ── qe 回测 ───────────────────────────────────────────────────────────

def _make_qe_strategy(strategy_name: str, symbol: str, params: dict):
    """创建 qe 策略实例"""
    if strategy_name == "dual_ma":
        from strategies.dual_ma import DualMA
        return DualMA(symbol,
                      fast_period=params.get("fast", 5),
                      slow_period=params.get("slow", 20))
    if strategy_name == "macd_cross":
        from strategies.macd_cross import MACDCross
        return MACDCross(symbol,
                         fast=params.get("fast", 12),
                         slow=params.get("slow", 26),
                         signal=params.get("signal", 9))
    if strategy_name == "rsi_reversal":
        from strategies.rsi_reversal import RSIReversal
        return RSIReversal(symbol,
                           period=params.get("period", 14),
                           oversold=params.get("oversold", 30.0),
                           overbought=params.get("overbought", 70.0))
    if strategy_name == "bollinger_breakout":
        from strategies.bollinger_breakout import BollingerBreakout
        return BollingerBreakout(symbol,
                                 period=params.get("period", 20),
                                 num_std=params.get("num_std", 2.0))
    raise ValueError(f"unknown strategy: {strategy_name}")


def run_qe_backtest(ohlcv_rows, symbol: str, strategy_name: str,
                    cash: float, commission: float, slippage: float,
                    params: dict):
    """运行 qe 引擎回测，返回 dict 结果 + fills 列表"""
    import qe

    engine = qe.Engine()
    sid = engine.symbol_id(symbol)

    bars = []
    for row in ohlcv_rows:
        bar = qe.Bar()
        bar.timestamp_ms = row[0]
        bar.open = row[1]
        bar.high = row[2]
        bar.low = row[3]
        bar.close = row[4]
        bar.volume = row[5]
        bar.quote_volume = 0.0
        bars.append(bar)
    engine.add_feed_bars(sid, bars)

    cfg = qe.SimBrokerConfig()
    cfg.cash = cash
    cfg.commission_rate = commission
    cfg.slippage = slippage
    engine.set_broker(cfg)

    strat = _make_qe_strategy(strategy_name, symbol, params)
    engine.add_strategy(strat)
    result = engine.run()

    # 从 fills 构建逐笔 trade（配对 BUY→SELL）
    ts_to_date = {}
    for row in ohlcv_rows:
        ts_to_date[row[0]] = datetime.fromtimestamp(row[0] / 1000, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S")

    fills = result.fills if hasattr(result, 'fills') else []
    trades = _pair_fills_to_trades(fills, ts_to_date) if fills else []

    return {
        "initial_cash": result.initial_cash,
        "final_equity": result.final_equity,
        "total_return": result.total_return,
        "annual_return": result.annual_return,
        "sharpe": result.sharpe,
        "sortino": result.sortino,
        "calmar": result.calmar,
        "max_drawdown": result.max_drawdown,
        "total_trades": result.total_trades,
        "win_rate": result.win_rate,
        "profit_factor": result.profit_factor,
        "equity_curve": list(result.equity_curve),
        "trades": trades,
    }


def _pair_fills_to_trades(fills, ts_to_date):
    """将 BUY/SELL fills 配对成 trades"""
    import qe
    trades = []
    open_fill = None
    for f in fills:
        if f.side == qe.Side.BUY:
            open_fill = f
        elif f.side == qe.Side.SELL and open_fill is not None:
            pnl_pct = (f.price - open_fill.price) / open_fill.price * 100
            trades.append({
                "entry_time": ts_to_date.get(open_fill.timestamp_ms,
                    datetime.fromtimestamp(open_fill.timestamp_ms / 1000, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S")),
                "exit_time": ts_to_date.get(f.timestamp_ms,
                    datetime.fromtimestamp(f.timestamp_ms / 1000, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S")),
                "entry_price": open_fill.price,
                "exit_price": f.price,
                "profit_pct": pnl_pct,
            })
            open_fill = None
    return trades


# ── freqtrade 回测 ────────────────────────────────────────────────────

_FT_RUNNER_SCRIPT = r'''
"""freqtrade 回测 runner — 在 .ft_env venv 中执行"""
import json
import sys
from pathlib import Path

from freqtrade.data.converter import ohlcv_to_dataframe
from freqtrade.data.history import get_datahandler
from freqtrade.enums import CandleType, RunMode
from freqtrade.optimize.backtesting import Backtesting


def main():
    config = json.loads(sys.stdin.read())

    ohlcv_rows = config["ohlcv"]
    pair = config["pair"]
    timeframe = config["timeframe"]
    strategy_name = config["ft_strategy"]
    strategy_dir = config["strategy_dir"]
    cash = config["cash"]
    fee = config["fee"]

    # 1) 写 OHLCV 数据
    data_dir = Path(config["data_dir"])
    data_dir.mkdir(parents=True, exist_ok=True)

    df = ohlcv_to_dataframe(ohlcv_rows, timeframe, pair,
                            fill_missing=False, drop_incomplete=False)

    dh = get_datahandler(data_dir, data_format="feather")
    dh.ohlcv_store(pair, timeframe, df, CandleType.SPOT)

    # 2) freqtrade 配置
    ft_config = {
        "stake_currency": "USDT",
        "stake_amount": "unlimited",
        "dry_run_wallet": cash,
        "trading_mode": "spot",
        "margin_mode": "",
        "max_open_trades": 1,
        "timeframe": timeframe,
        "strategy": strategy_name,
        "strategy_path": strategy_dir,
        "datadir": data_dir,
        "data_format_ohlcv": "feather",
        "user_data_dir": Path(data_dir).parent,
        "exchange": {
            "name": "binance",
            "pair_whitelist": [pair],
        },
        "fee": fee,
        "pairs": [pair],
        "runmode": RunMode.BACKTEST,
        "startup_candle_count": 50,
        "internals": {"process_throttle_secs": 5},
        "export": "none",
        "entry_pricing": {
            "price_side": "same",
            "use_order_book": False,
            "order_book_top": 1,
        },
        "exit_pricing": {
            "price_side": "same",
            "use_order_book": False,
            "order_book_top": 1,
        },
        "pairlists": [{"method": "StaticPairList"}],
    }

    # 3) 运行
    bt = Backtesting(ft_config)
    bt.start()

    # 4) 提取结果
    results = bt.results
    if not results or "strategy" not in results:
        print(json.dumps({"error": "no results"}))
        return

    strat_key = list(results["strategy"].keys())[0]
    sr = results["strategy"][strat_key]

    trades = sr.get("trades", [])
    total_trades = sr.get("total_trades", len(trades))
    final_equity = float(sr.get("final_balance", cash))
    total_return = (final_equity - cash) / cash if cash > 0 else 0.0

    trade_list = []
    for t in trades:
        trade_list.append({
            "entry_time": str(t.get("open_date", "")),
            "exit_time": str(t.get("close_date", "")),
            "entry_price": float(t.get("open_rate", 0)),
            "exit_price": float(t.get("close_rate", 0)),
            "profit_pct": float(t.get("profit_ratio", 0)) * 100,
            "profit_abs": float(t.get("profit_abs", 0)),
            "amount": float(t.get("stake_amount", 0)),
        })

    output = {
        "initial_cash": cash,
        "final_equity": final_equity,
        "total_return": total_return,
        "max_drawdown": float(sr.get("max_drawdown_account", 0)),
        "sharpe": float(sr.get("sharpe", 0)),
        "sortino": float(sr.get("sortino", 0)),
        "calmar": float(sr.get("calmar", 0)),
        "total_trades": total_trades,
        "win_rate": float(sr.get("winrate", 0)),
        "profit_factor": float(sr.get("profit_factor", 0)),
        "trades": trade_list,
    }
    print(json.dumps(output))


if __name__ == "__main__":
    main()
'''


def run_ft_backtest(ohlcv_rows, symbol: str, strategy_name: str,
                    interval: str, cash: float, commission: float):
    """通过 subprocess 在 .ft_env 中运行 freqtrade 回测"""
    if not FT_PYTHON.exists():
        raise RuntimeError(f"freqtrade venv not found at {FT_VENV}")

    ft_strategy = STRATEGY_MAP.get(strategy_name)
    if not ft_strategy:
        raise ValueError(f"no freqtrade strategy for: {strategy_name}")

    pair = symbol.replace("USDT", "/USDT")

    with tempfile.TemporaryDirectory(prefix="ft_compare_") as tmpdir:
        data_dir = os.path.join(tmpdir, "data")

        input_data = json.dumps({
            "ohlcv": ohlcv_rows,
            "pair": pair,
            "timeframe": interval,
            "ft_strategy": ft_strategy,
            "strategy_dir": str(FT_STRATEGIES_DIR),
            "data_dir": data_dir,
            "cash": cash,
            "fee": commission,
        })

        runner_path = os.path.join(tmpdir, "ft_runner.py")
        with open(runner_path, "w") as f:
            f.write(_FT_RUNNER_SCRIPT)

        proc = subprocess.run(
            [str(FT_PYTHON), runner_path],
            input=input_data,
            capture_output=True,
            text=True,
            timeout=300,
        )

        if proc.returncode != 0:
            print("=== freqtrade stderr ===", file=sys.stderr)
            print(proc.stderr[-3000:], file=sys.stderr)
            raise RuntimeError(f"freqtrade backtesting failed (exit {proc.returncode})")

        stdout_lines = proc.stdout.strip().split("\n")
        for line in reversed(stdout_lines):
            line = line.strip()
            if line.startswith("{"):
                return json.loads(line)

        raise RuntimeError("freqtrade did not return JSON results")


# ── 逐笔 trade 对比 ──────────────────────────────────────────────────

def align_trades(qe_trades: list, ft_trades: list, tolerance_bars: int = 2):
    """逐笔对齐 trades，按入场时间匹配

    返回: (matched, qe_only, ft_only)
    matched: list of (qe_trade, ft_trade, entry_price_diff_pct)
    """
    matched = []
    ft_used = set()

    for qt in qe_trades:
        qe_entry = qt["entry_time"][:16]  # 精确到分钟
        best_match = None
        best_idx = -1
        best_diff = float("inf")

        for i, ft in enumerate(ft_trades):
            if i in ft_used:
                continue
            ft_entry = ft["entry_time"][:16]
            # 简单匹配：时间字符串相同或相差很小
            if qe_entry == ft_entry:
                best_match = ft
                best_idx = i
                break
            # 容忍 1 bar 偏移（T+1 差异）
            try:
                qe_dt = datetime.strptime(qe_entry, "%Y-%m-%d %H:%M")
                ft_dt = datetime.strptime(ft_entry, "%Y-%m-%d %H:%M")
                diff_hours = abs((qe_dt - ft_dt).total_seconds()) / 3600
                if diff_hours <= tolerance_bars * 24 and diff_hours < best_diff:
                    best_diff = diff_hours
                    best_match = ft
                    best_idx = i
            except ValueError:
                pass

        if best_match is not None:
            entry_diff = abs(qt["entry_price"] - best_match["entry_price"]) / best_match["entry_price"] * 100
            matched.append((qt, best_match, entry_diff))
            ft_used.add(best_idx)

    qe_only = [qt for i, qt in enumerate(qe_trades) if not any(m[0] is qt for m in matched)]
    ft_only = [ft for i, ft in enumerate(ft_trades) if i not in ft_used]

    return matched, qe_only, ft_only


# ── 对比输出 ──────────────────────────────────────────────────────────

def _fmt_pct(v):
    return f"{v * 100:.2f}%"

def _fmt_f(v, prec=4):
    return f"{v:.{prec}f}"

def _delta(a, b):
    if abs(b) < 1e-12:
        return "N/A"
    return f"{(a - b) / abs(b) * 100:+.2f}%"


def compare_results(qe_res: dict, ft_res: dict, strategy_name: str,
                    symbol: str, interval: str, do_plot: bool = False,
                    output_path: str = None, verbose: bool = True):
    """打印对比表，返回 summary dict"""

    rows = [
        ("初始资金", f"${qe_res['initial_cash']:.0f}", f"${ft_res['initial_cash']:.0f}", ""),
        ("最终权益", f"${qe_res['final_equity']:.2f}", f"${ft_res['final_equity']:.2f}",
         _delta(qe_res['final_equity'], ft_res['final_equity'])),
        ("总收益率", _fmt_pct(qe_res['total_return']), _fmt_pct(ft_res['total_return']),
         _delta(qe_res['total_return'], ft_res['total_return'])),
        ("最大回撤", _fmt_pct(qe_res['max_drawdown']), _fmt_pct(ft_res.get('max_drawdown', 0)),
         _delta(qe_res['max_drawdown'], ft_res.get('max_drawdown', 0))),
        ("Sharpe", _fmt_f(qe_res['sharpe']), _fmt_f(ft_res.get('sharpe', 0)),
         _delta(qe_res['sharpe'], ft_res.get('sharpe', 0))),
        ("Sortino", _fmt_f(qe_res.get('sortino', 0)), _fmt_f(ft_res.get('sortino', 0)),
         _delta(qe_res.get('sortino', 0), ft_res.get('sortino', 0))),
        ("总交易笔数", str(qe_res['total_trades']), str(ft_res['total_trades']),
         f"{qe_res['total_trades'] - ft_res['total_trades']:+d}"),
        ("胜率", _fmt_pct(qe_res['win_rate']), _fmt_pct(ft_res['win_rate']),
         _delta(qe_res['win_rate'], ft_res['win_rate'])),
        ("利润因子", _fmt_f(qe_res.get('profit_factor', 0), 2),
         _fmt_f(ft_res.get('profit_factor', 0), 2),
         _delta(qe_res.get('profit_factor', 0), ft_res.get('profit_factor', 0))),
    ]

    if verbose:
        header = f"\n{'='*65}"
        print(header)
        print(f"  对拍: {strategy_name} | {symbol} | {interval}")
        print(header)
        print(f"  {'指标':<14} {'quant-engine':>14} {'freqtrade':>14} {'偏差':>12}")
        print(f"  {'-'*56}")
        for name, qe_val, ft_val, delta in rows:
            print(f"  {name:<14} {qe_val:>14} {ft_val:>14} {delta:>12}")
        print(header)

    # 逐笔 trade 对比
    qe_trades = qe_res.get("trades", [])
    ft_trades = ft_res.get("trades", [])
    trade_match_rate = 0.0
    avg_entry_price_diff = 0.0
    avg_pnl_diff = 0.0

    if qe_trades and ft_trades:
        matched, qe_only, ft_only = align_trades(qe_trades, ft_trades)
        total = max(len(qe_trades), len(ft_trades))
        trade_match_rate = len(matched) / total * 100 if total > 0 else 0

        if matched:
            entry_diffs = [m[2] for m in matched]
            avg_entry_price_diff = sum(entry_diffs) / len(entry_diffs)
            pnl_diffs = [abs(m[0]["profit_pct"] - m[1]["profit_pct"]) for m in matched]
            avg_pnl_diff = sum(pnl_diffs) / len(pnl_diffs)

        if verbose:
            print(f"\n  [逐笔交易对齐]")
            print(f"  配对成功: {len(matched)}/{total} ({trade_match_rate:.0f}%)")
            print(f"  qe 独有:  {len(qe_only)} 笔")
            print(f"  ft 独有:  {len(ft_only)} 笔")
            if matched:
                print(f"  平均入场价偏差: {avg_entry_price_diff:.4f}%")
                print(f"  平均单笔盈亏偏差: {avg_pnl_diff:.2f}pp")

            # 显示前 10 对匹配的 trade
            if matched:
                print(f"\n  配对交易明细（前 10 笔）:")
                print(f"  {'#':>3} {'qe入场':>12} {'ft入场':>12} {'价差%':>8} "
                      f"{'qe盈亏%':>8} {'ft盈亏%':>8} {'差':>6}")
                print(f"  {'-'*62}")
                for i, (qt, ft, ediff) in enumerate(matched[:10], 1):
                    pdiff = qt["profit_pct"] - ft["profit_pct"]
                    print(f"  {i:>3} {qt['entry_price']:>12.2f} {ft['entry_price']:>12.2f} "
                          f"{ediff:>7.3f}% {qt['profit_pct']:>7.2f}% "
                          f"{ft['profit_pct']:>7.2f}% {pdiff:>+5.2f}")

    if verbose:
        print(f"\n  [已知差异]")
        print(f"  - qe: T+1 成交（下一根 bar open）; ft: T+0 成交（信号 bar open）")
        print(f"  - qe: 固定 0.1 单位; ft: 全仓（影响收益率%，不影响胜率/利润因子）")
        print(f"  - 指标实现差异：qe 自研 C++ vs ta-lib（可能有小数精度偏差）")

    if do_plot:
        _plot_comparison(qe_res, ft_res, strategy_name, symbol, interval, output_path)

    # 返回 summary
    trade_diff = abs(qe_res['total_trades'] - ft_res['total_trades'])
    return {
        "strategy": strategy_name,
        "symbol": symbol,
        "interval": interval,
        "qe_trades": qe_res['total_trades'],
        "ft_trades": ft_res['total_trades'],
        "trade_diff": trade_diff,
        "qe_win_rate": qe_res['win_rate'],
        "ft_win_rate": ft_res['win_rate'],
        "win_rate_diff_pp": abs(qe_res['win_rate'] - ft_res['win_rate']) * 100,
        "qe_pf": qe_res.get('profit_factor', 0),
        "ft_pf": ft_res.get('profit_factor', 0),
        "trade_match_rate": trade_match_rate,
        "avg_entry_price_diff": avg_entry_price_diff,
        "avg_pnl_diff": avg_pnl_diff,
    }


def _plot_comparison(qe_res: dict, ft_res: dict, strategy_name: str,
                     symbol: str, interval: str, output_path: str = None):
    """画权益曲线对比图"""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("  [跳过绘图] matplotlib 未安装")
        return

    equity = qe_res.get("equity_curve", [])
    if not equity:
        return

    fig, ax = plt.subplots(figsize=(14, 6))
    ax.plot(equity, label=f"quant-engine (final: ${equity[-1]:.2f})", linewidth=1.2)
    ft_eq = ft_res.get("final_equity", qe_res["initial_cash"])
    ax.axhline(y=ft_eq, color="orange", linestyle="--", alpha=0.7,
               label=f"freqtrade final: ${ft_eq:.2f}")
    ax.axhline(y=qe_res["initial_cash"], color="gray", linestyle=":", alpha=0.5)
    ax.set_title(f"对拍: {strategy_name} | {symbol} | {interval}")
    ax.set_xlabel("Bar #")
    ax.set_ylabel("Equity ($)")
    ax.legend()
    ax.grid(True, alpha=0.3)

    if not output_path:
        os.makedirs("output", exist_ok=True)
        output_path = f"output/compare_{strategy_name}_{symbol}_{interval}.png"
    os.makedirs(os.path.dirname(output_path) or ".", exist_ok=True)
    fig.savefig(output_path, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"\n  对比图: {output_path}")


# ── 批量汇总 ──────────────────────────────────────────────────────────

def print_batch_summary(summaries: list):
    """打印批量对拍汇总表"""
    print(f"\n{'='*100}")
    print(f"  批量对拍汇总 ({len(summaries)} 个场景)")
    print(f"{'='*100}")
    print(f"  {'策略':<20} {'品种':<10} {'周期':<5} "
          f"{'qe笔':>5} {'ft笔':>5} {'差':>4} "
          f"{'配对率':>6} {'胜率差pp':>8} {'利润因子qe':>9} {'利润因子ft':>9} "
          f"{'判定':>4}")
    print(f"  {'-'*96}")

    all_pass = True
    for s in summaries:
        # 判定标准（小样本放宽胜率容差）
        trade_ok = s["trade_diff"] <= 3
        n_trades = max(s["qe_trades"], s["ft_trades"])
        wr_tol = 5.0 if n_trades >= 30 else 15.0  # <30 笔交易时放宽到 15pp
        wr_ok = s["win_rate_diff_pp"] <= wr_tol
        match_ok = s["trade_match_rate"] >= 80 or s["qe_trades"] == 0
        passed = trade_ok and wr_ok and match_ok

        if not passed:
            all_pass = False

        verdict = "PASS" if passed else "FAIL"
        print(f"  {s['strategy']:<20} {s['symbol']:<10} {s['interval']:<5} "
              f"{s['qe_trades']:>5} {s['ft_trades']:>5} {s['trade_diff']:>+4} "
              f"{s['trade_match_rate']:>5.0f}% {s['win_rate_diff_pp']:>7.2f} "
              f"{s['qe_pf']:>9.2f} {s['ft_pf']:>9.2f} "
              f"{'  ' + verdict:>6}")

    print(f"  {'-'*96}")
    overall = "ALL PASS" if all_pass else "SOME FAILED"
    print(f"  总体判定: {overall}")
    print(f"{'='*100}")

    print(f"\n  [判定标准]")
    print(f"  - 交易笔数差 ≤ 3")
    print(f"  - 胜率差 ≤ 5pp（<30 笔交易时放宽到 15pp）")
    print(f"  - 逐笔配对率 ≥ 80%")
    print(f"  - 收益率不作为判定条件（仓位模型不同）")


# ── main ──────────────────────────────────────────────────────────────

def run_single(client, strategy: str, symbol: str, start: str, end: str,
               interval: str, cash: float, commission: float, slippage: float,
               plot: bool = False, output: str = None, verbose: bool = True):
    """运行单个场景的对拍"""
    if verbose:
        print(f"\n加载数据: {symbol} {interval} [{start}, {end}) ...")
    ohlcv = load_ohlcv_from_ch(client, symbol, start, end, interval)
    if verbose:
        print(f"  -> {len(ohlcv)} bars")
    if not ohlcv:
        print(f"  无数据，跳过")
        return None

    if verbose:
        print(f"运行 quant-engine ...")
    qe_res = run_qe_backtest(ohlcv, symbol, strategy,
                             cash=cash, commission=commission, slippage=slippage,
                             params={})

    if verbose:
        print(f"  -> qe: {qe_res['total_trades']} trades, "
              f"return={qe_res['total_return']*100:.2f}%")
        print(f"运行 freqtrade ...")

    ft_res = run_ft_backtest(ohlcv, symbol, strategy,
                             interval=interval, cash=cash, commission=commission)

    if verbose:
        print(f"  -> ft: {ft_res['total_trades']} trades, "
              f"return={ft_res['total_return']*100:.2f}%")

    return compare_results(qe_res, ft_res, strategy, symbol, interval,
                           do_plot=plot, output_path=output, verbose=verbose)


def main():
    parser = argparse.ArgumentParser(description="quant-engine vs freqtrade 对拍")
    parser.add_argument("--strategy", choices=list(STRATEGY_MAP.keys()))
    parser.add_argument("--symbol", help="e.g. BTCUSDT")
    parser.add_argument("--start", help="开始日期 YYYY-MM-DD")
    parser.add_argument("--end", help="结束日期 YYYY-MM-DD")
    parser.add_argument("--interval", default="1h",
                        choices=["1m", "5m", "15m", "1h", "4h", "1d"])
    parser.add_argument("--cash", type=float, default=10000.0)
    parser.add_argument("--commission", type=float, default=0.0004)
    parser.add_argument("--slippage", type=float, default=0.0)
    parser.add_argument("--plot", action="store_true")
    parser.add_argument("--output", "-o")
    parser.add_argument("--batch", action="store_true",
                        help="批量运行全部预定义场景")
    args = parser.parse_args()

    client = clickhouse_connect.get_client(
        host=CH_HOST, port=CH_PORT,
        username=CH_USER, password=CH_PASSWORD,
    )

    if args.batch:
        summaries = []
        for strat, sym, start, end, interval in BATCH_SCENARIOS:
            print(f"\n{'─'*65}")
            print(f"  场景: {strat} | {sym} | {interval} | [{start}, {end})")
            print(f"{'─'*65}")
            try:
                s = run_single(client, strat, sym, start, end, interval,
                               cash=args.cash, commission=args.commission,
                               slippage=args.slippage, verbose=True)
                if s:
                    summaries.append(s)
            except Exception as e:
                print(f"  ERROR: {e}")
                summaries.append({
                    "strategy": strat, "symbol": sym, "interval": interval,
                    "qe_trades": 0, "ft_trades": 0, "trade_diff": 999,
                    "qe_win_rate": 0, "ft_win_rate": 0, "win_rate_diff_pp": 999,
                    "qe_pf": 0, "ft_pf": 0, "trade_match_rate": 0,
                    "avg_entry_price_diff": 0, "avg_pnl_diff": 0,
                })

        if summaries:
            print_batch_summary(summaries)
    else:
        if not all([args.strategy, args.symbol, args.start, args.end]):
            parser.error("--strategy, --symbol, --start, --end are required (or use --batch)")
        run_single(client, args.strategy, args.symbol, args.start, args.end,
                   args.interval, cash=args.cash, commission=args.commission,
                   slippage=args.slippage, plot=args.plot, output=args.output)


if __name__ == "__main__":
    main()
