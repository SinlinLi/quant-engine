"""freqtrade 版 RSI 反转策略 — 与 qe RSIReversal 等价

逻辑：
  - RSI < 30 且无持仓 → 买入
  - RSI > 70 且有持仓 → 卖出
"""
import talib
from freqtrade.strategy import IStrategy
from pandas import DataFrame


class FtRSIReversal(IStrategy):
    INTERFACE_VERSION = 3

    minimal_roi = {"0": 100.0}
    stoploss = -1.0

    rsi_period = 14
    oversold = 30.0
    overbought = 70.0

    timeframe = "1h"
    can_short = False
    process_only_new_candles = True

    def populate_indicators(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        dataframe["rsi"] = talib.RSI(dataframe["close"], timeperiod=self.rsi_period)
        return dataframe

    def populate_entry_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        dataframe.loc[
            (dataframe["rsi"] < self.oversold)
            & (dataframe["rsi"].shift(1) >= self.oversold),
            "enter_long",
        ] = 1
        return dataframe

    def populate_exit_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        dataframe.loc[
            (dataframe["rsi"] > self.overbought),
            "exit_long",
        ] = 1
        return dataframe
