#include "test/test_framework.h"
#include "indicator/macd.h"

TEST(macd_not_ready_early) {
    qe::MACD macd(3, 5, 2);  // 短周期测试
    for (int i = 0; i < 4; ++i)
        macd.update(make_bar(i * 60000, 100 + i, 100 + i, 100 + i, 100 + i));

    ASSERT_FALSE(macd.ready());  // 需要 slow_period=5 根 bar
}

TEST(macd_ready_timing) {
    qe::MACD macd(3, 5, 2);  // signal needs 2 samples of MACD line

    // 5 bars: slow EMA ready, but signal only has 1 sample → not ready
    for (int i = 0; i < 5; ++i)
        macd.update(make_bar(i * 60000, 100 + i, 100 + i, 100 + i, 100 + i));
    ASSERT_FALSE(macd.ready());

    // 6th bar: signal has 2 samples → ready
    macd.update(make_bar(5 * 60000, 105, 105, 105, 105));
    ASSERT_TRUE(macd.ready());
}

TEST(macd_constant_price) {
    // 恒定价格 → fast EMA == slow EMA → MACD line = 0
    qe::MACD macd(3, 5, 2);
    for (int i = 0; i < 10; ++i)
        macd.update(make_bar(i * 60000, 100, 100, 100, 100));

    ASSERT_TRUE(macd.ready());
    ASSERT_NEAR(macd.value(), 0.0, 1e-6);
    ASSERT_NEAR(macd.signal(), 0.0, 1e-6);
    ASSERT_NEAR(macd.histogram(), 0.0, 1e-6);
}

TEST(macd_uptrend) {
    // 持续上涨 → fast EMA > slow EMA → MACD line > 0
    qe::MACD macd(3, 5, 2);
    for (int i = 0; i < 20; ++i)
        macd.update(make_bar(i * 60000, 100 + i * 5, 100 + i * 5, 100 + i * 5, 100 + i * 5));

    ASSERT_TRUE(macd.ready());
    ASSERT_TRUE(macd.value() > 0);  // fast tracks faster → higher → positive MACD
}
