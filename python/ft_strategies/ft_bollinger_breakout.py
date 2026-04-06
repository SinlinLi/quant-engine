"""freqtrade 版布林带突破策略 — 与 qe BollingerBreakout 等价

逻辑：
  - close > 上轨 且无持仓 → 买入
  - close < 中轨 且有持仓 → 卖出
"""
import talib
from freqtrade.strategy import IStrategy
from pandas import DataFrame


class FtBollingerBreakout(IStrategy):
    INTERFACE_VERSION = 3

    minimal_roi = {"0": 100.0}
    stoploss = -1.0

    bb_period = 20
    bb_std = 2.0

    timeframe = "1h"
    can_short = False
    process_only_new_candles = True

    def populate_indicators(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        upper, middle, lower = talib.BBANDS(
            dataframe["close"],
            timeperiod=self.bb_period,
            nbdevup=self.bb_std,
            nbdevdn=self.bb_std,
        )
        dataframe["bb_upper"] = upper
        dataframe["bb_middle"] = middle
        dataframe["bb_lower"] = lower
        return dataframe

    def populate_entry_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        dataframe.loc[
            (dataframe["close"] > dataframe["bb_upper"])
            & (dataframe["close"].shift(1) <= dataframe["bb_upper"].shift(1)),
            "enter_long",
        ] = 1
        return dataframe

    def populate_exit_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        dataframe.loc[
            (dataframe["close"] < dataframe["bb_middle"]),
            "exit_long",
        ] = 1
        return dataframe
