"""freqtrade 版 MACD 金叉死叉策略 — 与 qe MACDCross 等价

逻辑：
  - MACD histogram 由负转正（金叉）→ 买入
  - MACD histogram 由正转负（死叉）→ 卖出
"""
import talib
from freqtrade.strategy import IStrategy
from pandas import DataFrame


class FtMACDCross(IStrategy):
    INTERFACE_VERSION = 3

    minimal_roi = {"0": 100.0}
    stoploss = -1.0

    # 与 qe MACDCross 一致的参数
    fast_period = 12
    slow_period = 26
    signal_period = 9

    timeframe = "1h"
    can_short = False
    process_only_new_candles = True

    def populate_indicators(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        macd, signal, hist = talib.MACD(
            dataframe["close"],
            fastperiod=self.fast_period,
            slowperiod=self.slow_period,
            signalperiod=self.signal_period,
        )
        dataframe["macd"] = macd
        dataframe["macd_signal"] = signal
        dataframe["macd_hist"] = hist
        return dataframe

    def populate_entry_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        # 金叉：histogram 从 <=0 变为 >0
        dataframe.loc[
            (dataframe["macd_hist"] > 0)
            & (dataframe["macd_hist"].shift(1) <= 0),
            "enter_long",
        ] = 1
        return dataframe

    def populate_exit_trend(self, dataframe: DataFrame, metadata: dict) -> DataFrame:
        # 死叉：histogram 从 >=0 变为 <0
        dataframe.loc[
            (dataframe["macd_hist"] < 0)
            & (dataframe["macd_hist"].shift(1) >= 0),
            "exit_long",
        ] = 1
        return dataframe
