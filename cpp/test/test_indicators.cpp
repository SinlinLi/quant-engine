#include "test/test_framework.h"
#include "indicator/sma.h"
#include "indicator/ema.h"

// SMA(3) on close prices: 10, 20, 30, 40, 50
TEST(sma_basic) {
    qe::SMA sma(3);
    sma.update(make_bar(1, 10, 10, 10, 10));
    ASSERT_FALSE(sma.ready());

    sma.update(make_bar(2, 20, 20, 20, 20));
    ASSERT_FALSE(sma.ready());

    sma.update(make_bar(3, 30, 30, 30, 30));
    ASSERT_TRUE(sma.ready());
    ASSERT_NEAR(sma.value(), 20.0, 1e-12);  // (10+20+30)/3

    sma.update(make_bar(4, 40, 40, 40, 40));
    ASSERT_NEAR(sma.value(), 30.0, 1e-12);  // (20+30+40)/3

    sma.update(make_bar(5, 50, 50, 50, 50));
    ASSERT_NEAR(sma.value(), 40.0, 1e-12);  // (30+40+50)/3
}

TEST(sma_period_1) {
    qe::SMA sma(1);
    sma.update(make_bar(1, 0, 0, 0, 42.0));
    ASSERT_TRUE(sma.ready());
    ASSERT_NEAR(sma.value(), 42.0, 1e-12);
}

// EMA(3) on close: 10, 20, 30, 40
// multiplier = 2/(3+1) = 0.5
// bar1: count=1, sum=10
// bar2: count=2, sum=30
// bar3: count=3, SMA init → value = (10+20+30)/3 = 20.0
// bar4: count=4, EMA = (40 - 20) * 0.5 + 20 = 30.0
TEST(ema_basic) {
    qe::EMA ema(3);
    ema.update(make_bar(1, 10, 10, 10, 10));
    ASSERT_FALSE(ema.ready());

    ema.update(make_bar(2, 20, 20, 20, 20));
    ASSERT_FALSE(ema.ready());

    ema.update(make_bar(3, 30, 30, 30, 30));
    ASSERT_TRUE(ema.ready());
    ASSERT_NEAR(ema.value(), 20.0, 1e-12);

    ema.update(make_bar(4, 40, 40, 40, 40));
    ASSERT_TRUE(ema.ready());
    ASSERT_NEAR(ema.value(), 30.0, 1e-12);
}

TEST(ema_period_1) {
    // EMA(1): multiplier = 2/2 = 1.0
    // bar1: SMA init → value = 100
    // bar2: (200 - 100) * 1.0 + 100 = 200
    qe::EMA ema(1);
    ema.update(make_bar(1, 0, 0, 0, 100.0));
    ASSERT_TRUE(ema.ready());
    ASSERT_NEAR(ema.value(), 100.0, 1e-12);

    ema.update(make_bar(2, 0, 0, 0, 200.0));
    ASSERT_NEAR(ema.value(), 200.0, 1e-12);
}