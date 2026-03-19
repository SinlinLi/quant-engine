#include "test/test_framework.h"
#include "indicator/bollinger.h"

TEST(bollinger_constant_price) {
    // 恒定价格 → stddev=0 → upper == middle == lower
    qe::Bollinger bb(3, 2.0);
    for (int i = 0; i < 3; ++i)
        bb.update(make_bar(i * 60000, 100, 100, 100, 100));

    ASSERT_TRUE(bb.ready());
    ASSERT_NEAR(bb.value(), 100.0, 1e-12);   // middle = SMA = 100
    ASSERT_NEAR(bb.upper(), 100.0, 1e-12);   // stddev=0
    ASSERT_NEAR(bb.lower(), 100.0, 1e-12);
    ASSERT_NEAR(bb.bandwidth(), 0.0, 1e-12);
}

TEST(bollinger_basic) {
    qe::Bollinger bb(3, 2.0);
    // close: 10, 20, 30
    bb.update(make_bar(0, 10, 10, 10, 10));
    ASSERT_FALSE(bb.ready());
    bb.update(make_bar(60000, 20, 20, 20, 20));
    ASSERT_FALSE(bb.ready());
    bb.update(make_bar(120000, 30, 30, 30, 30));

    ASSERT_TRUE(bb.ready());
    // SMA = (10+20+30)/3 = 20
    ASSERT_NEAR(bb.value(), 20.0, 1e-12);
    // stddev = sqrt(((10-20)^2 + (20-20)^2 + (30-20)^2) / 3) = sqrt(200/3) ≈ 8.1650
    double stddev = std::sqrt(200.0 / 3.0);
    ASSERT_NEAR(bb.upper(), 20.0 + 2.0 * stddev, 1e-6);
    ASSERT_NEAR(bb.lower(), 20.0 - 2.0 * stddev, 1e-6);
}

TEST(bollinger_not_ready) {
    qe::Bollinger bb(20, 2.0);
    for (int i = 0; i < 19; ++i)
        bb.update(make_bar(i * 60000, 100 + i, 100 + i, 100 + i, 100 + i));

    ASSERT_FALSE(bb.ready());
}
