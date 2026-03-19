#include "test/test_framework.h"
#include "indicator/rsi.h"

// RSI(14) 基本验证：全涨 → RSI=100，全跌 → RSI=0
TEST(rsi_all_up) {
    qe::RSI rsi(3);  // 短周期方便测试
    // 4 bars: 100, 110, 120, 130（需要 period+1=4 根 bar 才 ready）
    for (int i = 0; i < 4; ++i)
        rsi.update(make_bar(i * 60000, 100 + i * 10, 100 + i * 10, 100 + i * 10, 100 + i * 10));

    ASSERT_TRUE(rsi.ready());
    ASSERT_NEAR(rsi.value(), 100.0, 1e-6);  // 全涨，RSI=100
}

TEST(rsi_all_down) {
    qe::RSI rsi(3);
    for (int i = 0; i < 4; ++i)
        rsi.update(make_bar(i * 60000, 100 - i * 10, 100 - i * 10, 100 - i * 10, 100 - i * 10));

    ASSERT_TRUE(rsi.ready());
    ASSERT_NEAR(rsi.value(), 0.0, 1e-6);  // 全跌，RSI=0
}

TEST(rsi_mixed) {
    qe::RSI rsi(3);
    // close: 100, 110, 105, 115 → changes: +10, -5, +10
    double closes[] = {100, 110, 105, 115};
    for (int i = 0; i < 4; ++i)
        rsi.update(make_bar(i * 60000, closes[i], closes[i], closes[i], closes[i]));

    ASSERT_TRUE(rsi.ready());
    // avg_gain = (10+0+10)/3 = 6.667, avg_loss = (0+5+0)/3 = 1.667
    // RS = 6.667/1.667 = 4.0, RSI = 100 - 100/5 = 80
    ASSERT_NEAR(rsi.value(), 80.0, 1e-6);
}

TEST(rsi_not_ready) {
    qe::RSI rsi(14);
    for (int i = 0; i < 14; ++i)
        rsi.update(make_bar(i * 60000, 100 + i, 100 + i, 100 + i, 100 + i));

    ASSERT_FALSE(rsi.ready());  // 需要 period+1 根 bar
}
