"""freqtrade 版双均线策略 — 与 qe DualMA 等价

逻辑：
  - SMA(5) > SMA(20) 且无持仓 → 买入
  - SMA(5) < SMA(20) 且有持仓 → 卖出
"""
import talib
from freqtrade.strategy import IStrategy
from pandas import DataFrame


class FtDualMA(IStrategy):
    INTERFACE_VERSION = 3

    # 禁用 ROI / stoploss，完全由信号控制
    minimal_roi = {"0": 100.0}
    stoploss = -1.0

    # 与 qe DualMA 一致的参数
    fast_period = 5
    slow_period = 20

    timeframe = "1h"
    can_short = False
    process_only_new_candles = True

    def populate_indicators(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        dataframe["sma_fast"] = talib.SMA(dataframe["close"], timeperiod=self.fast_period)
        dataframe["sma_slow"] = talib.SMA(dataframe["close"], timeperiod=self.slow_period)
        return dataframe

    def populate_entry_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        # qe: fast > slow 且无持仓 → 买入
        # freqtrade 自动管理持仓状态，只需给出信号
        dataframe.loc[
            (dataframe["sma_fast"] > dataframe["sma_slow"])
            & (dataframe["sma_fast"].shift(1) <= dataframe["sma_slow"].shift(1)),
            "enter_long",
        ] = 1
        return dataframe

    def populate_exit_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        # qe: fast < slow 且有持仓 → 卖出
        dataframe.loc[
            (dataframe["sma_fast"] < dataframe["sma_slow"]),
            "exit_long",
        ] = 1
        return dataframe
