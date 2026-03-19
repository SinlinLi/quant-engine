#include "test/test_framework.h"
#include "indicator/ring_buffer.h"

TEST(ring_buffer_basic) {
    qe::RingBuffer rb(3);
    ASSERT_EQ(rb.count(), (size_t)0);
    ASSERT_FALSE(rb.full());

    rb.push(1.0);
    rb.push(2.0);
    ASSERT_EQ(rb.count(), (size_t)2);
    ASSERT_FALSE(rb.full());
    ASSERT_NEAR(rb[0], 2.0, 1e-12);  // 最新
    ASSERT_NEAR(rb[1], 1.0, 1e-12);  // 前一个
}

TEST(ring_buffer_wrap) {
    qe::RingBuffer rb(3);
    rb.push(1.0);
    rb.push(2.0);
    rb.push(3.0);
    ASSERT_TRUE(rb.full());
    ASSERT_NEAR(rb[0], 3.0, 1e-12);
    ASSERT_NEAR(rb[1], 2.0, 1e-12);
    ASSERT_NEAR(rb[2], 1.0, 1e-12);

    // 溢出一圈
    rb.push(4.0);
    ASSERT_TRUE(rb.full());
    ASSERT_EQ(rb.count(), (size_t)3);
    ASSERT_NEAR(rb[0], 4.0, 1e-12);
    ASSERT_NEAR(rb[1], 3.0, 1e-12);
    ASSERT_NEAR(rb[2], 2.0, 1e-12);
}

TEST(ring_buffer_sum) {
    qe::RingBuffer rb(4);
    rb.push(10.0);
    rb.push(20.0);
    rb.push(30.0);
    ASSERT_NEAR(rb.sum(), 60.0, 1e-12);

    rb.push(40.0);
    ASSERT_NEAR(rb.sum(), 100.0, 1e-12);

    rb.push(50.0);  // 挤掉 10
    ASSERT_NEAR(rb.sum(), 140.0, 1e-12);
}