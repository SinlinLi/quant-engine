#include "test/test_framework.h"
#include "data/csv_feed.h"

TEST(csv_feed_iteration) {
    std::vector<qe::Bar> bars = {
        make_bar(1000, 10, 11, 9, 10),
        make_bar(2000, 20, 21, 19, 20),
        make_bar(3000, 30, 31, 29, 30),
    };
    qe::CsvFeed feed(0, std::move(bars));

    // first next() → bar[0]
    ASSERT_TRUE(feed.next());
    ASSERT_EQ(feed.timestamp(), (int64_t)1000);
    ASSERT_NEAR(feed.current_bar().close, 10.0, 1e-12);

    // second next() → bar[1]
    ASSERT_TRUE(feed.next());
    ASSERT_EQ(feed.timestamp(), (int64_t)2000);

    // third next() → bar[2]
    ASSERT_TRUE(feed.next());
    ASSERT_EQ(feed.timestamp(), (int64_t)3000);

    // end
    ASSERT_FALSE(feed.next());
}

TEST(csv_feed_empty) {
    std::vector<qe::Bar> bars;
    qe::CsvFeed feed(0, std::move(bars));
    ASSERT_FALSE(feed.next());
}

TEST(csv_feed_single_bar) {
    std::vector<qe::Bar> bars = {make_bar(5000, 50, 55, 45, 52)};
    qe::CsvFeed feed(42, std::move(bars));
    ASSERT_EQ(feed.symbol_id(), (uint16_t)42);
    ASSERT_TRUE(feed.next());
    ASSERT_NEAR(feed.current_bar().open, 50.0, 1e-12);
    ASSERT_NEAR(feed.current_bar().high, 55.0, 1e-12);
    ASSERT_FALSE(feed.next());
}