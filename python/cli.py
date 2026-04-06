#!/usr/bin/env python3
"""quant-engine 命令行入口

Usage:
    python3 cli.py collect BTCUSDT --start 2024-01-01 --end 2024-01-31
    python3 cli.py collect BTCUSDT ETHUSDT --start 2024-01-01 --end 2024-12-31 --interval 1h
    python3 cli.py backtest --strategy dual_ma --symbols BTCUSDT --start 2024-01-01 --end 2024-01-31
    python3 cli.py list-data
"""
import argparse
import sys
import os
import uuid
from datetime import datetime, timezone

# C++ build 目录
BUILD_DIR = os.environ.get("QE_BUILD_DIR",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cpp", "build"))
sys.path.insert(0, os.path.abspath(BUILD_DIR))

import clickhouse_connect


def cmd_collect(args):
    """采集 Binance K 线数据到 ClickHouse"""
    from data_collector import collect

    for symbol in args.symbols:
        print(f"Collecting {symbol} {args.interval} from {args.start} to {args.end} ...")
        count = collect(
            symbol, args.start, args.end,
            interval=args.interval,
            ch_host=args.ch_host, ch_port=args.ch_port,
            ch_user=args.ch_user, ch_password=args.ch_password,
        )
        print(f"  -> {count} bars written")


def cmd_list_data(args):
    """查看 ClickHouse 中已有的数据"""
    client = clickhouse_connect.get_client(
        host=args.ch_host, port=args.ch_port,
        username=args.ch_user, password=args.ch_password,
    )
    result = client.query(
        "SELECT symbol, count() AS bars, "
        "min(open_time) AS first, max(open_time) AS last "
        "FROM qe.klines_1m GROUP BY symbol ORDER BY symbol"
    )
    if not result.result_rows:
        print("No data in qe.klines_1m")
        return

    print(f"{'Symbol':<15} {'Bars':>10} {'First':>22} {'Last':>22}")
    print("-" * 72)
    for row in result.result_rows:
        print(f"{row[0]:<15} {row[1]:>10} {row[2]!s:>22} {row[3]!s:>22}")


_INTERVAL_TO_CH = {
    "1m": None,           # 原始数据，不聚合
    "5m": "toStartOfFiveMinutes",
    "15m": "toStartOfFifteenMinutes",
    "1h": "toStartOfHour",
    "4h": "toStartOfInterval({col}, INTERVAL 4 HOUR)",
    "1d": "toStartOfDay",
}


def _load_bars_from_ch(client, symbol: str, start: str, end: str,
                       interval: str = "1m"):
    """从 ClickHouse 加载 bars 并转为 qe.Bar 列表

    interval: 1m, 5m, 15m, 1h, 4h, 1d
    非 1m 时从 klines_1m 实时聚合 OHLCV。
    """
    import qe

    if interval not in _INTERVAL_TO_CH:
        raise ValueError(
            f"unsupported interval '{interval}', "
            f"choose from: {', '.join(_INTERVAL_TO_CH)}")

    if interval == "1m":
        query = (
            "SELECT toInt64(toUnixTimestamp64Milli(open_time)), "
            "open, high, low, close, volume, quote_volume "
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
            f"argMax(close, open_time), sum(volume), sum(quote_volume) "
            f"FROM ("
            f"  SELECT {bucket} AS bucket, open_time, open, high, low, close, "
            f"         volume, quote_volume "
            f"  FROM qe.klines_1m "
            f"  WHERE symbol = %(symbol)s "
            f"  AND open_time >= %(start)s AND open_time < %(end)s"
            f") "
            f"GROUP BY bucket ORDER BY bucket"
        )

    result = client.query(query,
                          parameters={"symbol": symbol, "start": start, "end": end})

    bars = []
    for row in result.result_rows:
        bar = qe.Bar()
        bar.timestamp_ms = row[0]
        bar.open = row[1]
        bar.high = row[2]
        bar.low = row[3]
        bar.close = row[4]
        bar.volume = row[5]
        bar.quote_volume = row[6]
        bars.append(bar)
    return bars


def _load_strategy(name: str, symbols: list[str], params: dict):
    """按名称加载策略"""
    if name == "dual_ma":
        from strategies.dual_ma import DualMA
        fast = params.get("fast", 5)
        slow = params.get("slow", 20)
        return DualMA(symbols[0], fast_period=fast, slow_period=slow)
    if name == "macd_cross":
        from strategies.macd_cross import MACDCross
        fast = params.get("fast", 12)
        slow = params.get("slow", 26)
        signal = params.get("signal", 9)
        return MACDCross(symbols[0], fast=fast, slow=slow, signal=signal)
    if name == "momentum_rotation":
        from strategies.momentum_rotation import MomentumRotation
        lookback = params.get("lookback", 20)
        return MomentumRotation(symbols, lookback=lookback)
    raise ValueError(f"unknown strategy: {name}")


def cmd_backtest(args):
    """运行回测"""
    import qe

    client = clickhouse_connect.get_client(
        host=args.ch_host, port=args.ch_port,
        username=args.ch_user, password=args.ch_password,
    )
    engine = qe.Engine()

    # 加载数据
    bars_by_symbol = {}
    for symbol in args.symbols:
        bars = _load_bars_from_ch(client, symbol, args.start, args.end,
                                     interval=args.interval)
        if not bars:
            print(f"Warning: no data for {symbol} in [{args.start}, {args.end})")
            continue
        sid = engine.symbol_id(symbol)
        engine.add_feed_bars(sid, bars)
        bars_by_symbol[symbol] = bars
        print(f"Loaded {len(bars)} bars for {symbol}")

    # 配置 Broker
    cfg = qe.SimBrokerConfig()
    cfg.cash = args.cash
    cfg.commission_rate = args.commission
    cfg.slippage = args.slippage
    engine.set_broker(cfg)

    # 加载策略
    params = {}
    if args.params:
        for p in args.params:
            if "=" not in p:
                raise SystemExit(f"Invalid param format: '{p}' (expected key=value)")
            k, v = p.split("=", 1)
            try:
                params[k] = int(v)
            except ValueError:
                try:
                    params[k] = float(v)
                except ValueError:
                    raise SystemExit(f"Param '{k}' value '{v}' is not a number")

    strategy = _load_strategy(args.strategy, args.symbols, params)
    engine.add_strategy(strategy)

    # 运行
    print(f"\nRunning backtest: {args.strategy} on {','.join(args.symbols)} ...")
    result = engine.run()

    # 打印结果
    print(f"\n{'='*40}")
    print(f"Initial cash:   ${result.initial_cash:.2f}")
    print(f"Final equity:   ${result.final_equity:.2f}")
    print(f"Total return:   {result.total_return * 100:.2f}%")
    print(f"Annual return:  {result.annual_return * 100:.2f}%")
    print(f"Sharpe ratio:   {result.sharpe:.4f}")
    print(f"Max drawdown:   {result.max_drawdown * 100:.2f}%")
    print(f"Total trades:   {result.total_trades}")
    print(f"Win rate:       {result.win_rate * 100:.2f}%")
    print(f"Equity samples: {len(result.equity_curve)}")

    # 可视化
    if args.plot:
        from plot_results import plot_backtest
        output_path = args.output or f"output/{args.strategy}_{'-'.join(args.symbols)}_{args.start}_{args.end}.png"
        plot_backtest(result, bars_by_symbol, args.strategy, params, output_path)

    # 写入 ClickHouse
    if not args.no_save:
        # 确保 backtest_runs 表存在
        client.command("""
            CREATE TABLE IF NOT EXISTS qe.backtest_runs (
                run_id String,
                strategy String,
                params String,
                start_time String,
                end_time String,
                symbols Array(String),
                sharpe Float64,
                max_drawdown Float64,
                annual_return Float64,
                total_return Float64,
                total_trades UInt32,
                win_rate Float64,
                sortino Float64,
                calmar Float64,
                profit_factor Float64,
                created_at DateTime DEFAULT now()
            ) ENGINE = MergeTree() ORDER BY (created_at, run_id)
        """)

        run_id = str(uuid.uuid4())
        client.insert("qe.backtest_runs", [[
            run_id,
            args.strategy,
            str(params),
            args.start,
            args.end,
            args.symbols,
            result.sharpe,
            result.max_drawdown,
            result.annual_return,
            result.total_return,
            result.total_trades,
            result.win_rate,
            result.sortino,
            result.calmar,
            result.profit_factor,
        ]], column_names=[
            "run_id", "strategy", "params", "start_time", "end_time",
            "symbols", "sharpe", "max_drawdown", "annual_return",
            "total_return", "total_trades", "win_rate",
            "sortino", "calmar", "profit_factor",
        ])
        print(f"\nResults saved to ClickHouse (run_id: {run_id[:8]}...)")


def main():
    parser = argparse.ArgumentParser(description="quant-engine CLI")
    parser.add_argument("--ch-host", default="localhost", help="ClickHouse host")
    parser.add_argument("--ch-port", type=int, default=8123, help="ClickHouse HTTP port")
    parser.add_argument("--ch-user", default=os.environ.get("QE_CH_USER", "default"),
                        help="ClickHouse user (env: QE_CH_USER)")
    parser.add_argument("--ch-password", default=os.environ.get("QE_CH_PASSWORD", ""),
                        help="ClickHouse password (env: QE_CH_PASSWORD)")
    sub = parser.add_subparsers(dest="command", required=True)

    # collect
    p_collect = sub.add_parser("collect", help="采集 Binance K 线数据")
    p_collect.add_argument("symbols", nargs="+", help="交易对 (e.g. BTCUSDT)")
    p_collect.add_argument("--start", required=True, help="开始日期 (YYYY-MM-DD)")
    p_collect.add_argument("--end", required=True, help="结束日期 (YYYY-MM-DD)")
    p_collect.add_argument("--interval", default="1m", help="K 线周期 (default: 1m)")

    # backtest
    p_bt = sub.add_parser("backtest", help="运行回测")
    p_bt.add_argument("--strategy", required=True, help="策略名 (e.g. dual_ma)")
    p_bt.add_argument("--symbols", nargs="+", required=True, help="交易对")
    p_bt.add_argument("--start", required=True, help="开始日期")
    p_bt.add_argument("--end", required=True, help="结束日期")
    p_bt.add_argument("--cash", type=float, default=10000.0, help="初始资金")
    p_bt.add_argument("--commission", type=float, default=0.0004, help="手续费率")
    p_bt.add_argument("--slippage", type=float, default=0.0001, help="滑点")
    p_bt.add_argument("--interval", default="1m",
                       help="K 线周期: 1m, 5m, 15m, 1h, 4h, 1d (default: 1m)")
    p_bt.add_argument("--params", nargs="*", help="策略参数 (key=value)")
    p_bt.add_argument("--no-save", action="store_true", help="不保存结果到 ClickHouse")
    p_bt.add_argument("--plot", action="store_true", help="生成回测结果图表")
    p_bt.add_argument("--output", "-o", help="图表输出路径 (默认: output/{strategy}_{symbol}_{dates}.png)")

    # list-data
    sub.add_parser("list-data", help="查看已有数据")

    args = parser.parse_args()

    if args.command == "collect":
        cmd_collect(args)
    elif args.command == "backtest":
        cmd_backtest(args)
    elif args.command == "list-data":
        cmd_list_data(args)


if __name__ == "__main__":
    main()
