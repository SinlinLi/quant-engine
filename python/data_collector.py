"""Binance K线数据采集 → ClickHouse

从 Binance REST API 分页拉取历史 K 线，批量写入 ClickHouse qe.bars 表。
公开数据，无需 API Key。

Usage:
    from data_collector import collect
    collect("BTCUSDT", "2024-01-01", "2024-01-31", interval="1m")
"""
import time
import urllib.error
import urllib.request
import json
from datetime import datetime, timezone

import clickhouse_connect


BINANCE_KLINES_URL = "https://api.binance.com/api/v3/klines"
BATCH_LIMIT = 1000  # Binance 单次最多 1000 条


def _ts(date_str: str) -> int:
    """日期字符串 → 毫秒时间戳 (UTC)"""
    dt = datetime.strptime(date_str, "%Y-%m-%d").replace(tzinfo=timezone.utc)
    return int(dt.timestamp() * 1000)


def _fetch_klines(symbol: str, interval: str, start_ms: int, end_ms: int,
                   max_retries: int = 3) -> list:
    """从 Binance API 拉取一批 K 线（带指数退避重试）"""
    params = (
        f"symbol={symbol}&interval={interval}"
        f"&startTime={start_ms}&endTime={end_ms}&limit={BATCH_LIMIT}"
    )
    url = f"{BINANCE_KLINES_URL}?{params}"
    req = urllib.request.Request(url)
    for attempt in range(max_retries):
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                return json.loads(resp.read())
        except (urllib.error.URLError, urllib.error.HTTPError, OSError) as e:
            if attempt == max_retries - 1:
                raise
            wait = 2 ** attempt
            print(f"  Retry {attempt + 1}/{max_retries} after {wait}s: {e}")
            time.sleep(wait)
    return []


def collect(
    symbol: str,
    start_date: str,
    end_date: str,
    interval: str = "1m",
    ch_host: str = "localhost",
    ch_port: int = 8123,
    ch_user: str = "default",
    ch_password: str = "",
) -> int:
    """采集 Binance K 线并写入 ClickHouse

    Returns:
        写入的总行数
    """
    client = clickhouse_connect.get_client(
        host=ch_host, port=ch_port,
        username=ch_user, password=ch_password,
    )
    start_ms = _ts(start_date)
    end_ms = _ts(end_date) - 1  # 不含结束日

    total = 0
    cursor = start_ms

    while cursor <= end_ms:
        klines = _fetch_klines(symbol, interval, cursor, end_ms)
        if not klines:
            break

        rows = []
        for k in klines:
            # Binance kline: [open_time, open, high, low, close, volume,
            #                  close_time, quote_volume, trades,
            #                  taker_buy_base_vol, taker_buy_quote_vol, ...]
            rows.append([
                symbol,
                datetime.fromtimestamp(k[0] / 1000, tz=timezone.utc),
                float(k[1]),  # open
                float(k[2]),  # high
                float(k[3]),  # low
                float(k[4]),  # close
                float(k[5]),  # volume
                datetime.fromtimestamp(k[6] / 1000, tz=timezone.utc),
                float(k[7]),  # quote_volume
                int(k[8]),    # trades
                float(k[9]),  # taker_buy_base_vol
                float(k[10]), # taker_buy_quote_vol
            ])

        client.insert(
            "qe.klines_1m",
            rows,
            column_names=["symbol", "open_time", "open", "high", "low",
                          "close", "volume", "close_time", "quote_volume",
                          "trades", "taker_buy_base_vol", "taker_buy_quote_vol"],
        )

        total += len(rows)
        # 下一批从最后一条的 close_time + 1 开始
        cursor = klines[-1][6] + 1

        if len(klines) < BATCH_LIMIT:
            break

        # 限速：Binance 公开 API 限制 ~1200 req/min
        time.sleep(0.1)

    return total
