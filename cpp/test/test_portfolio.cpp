#include "test/test_framework.h"
#include "core/portfolio.h"

TEST(portfolio_init) {
    qe::Portfolio p(10000.0);
    ASSERT_NEAR(p.cash(), 10000.0, 1e-12);
    ASSERT_NEAR(p.equity(), 10000.0, 1e-12);
    ASSERT_NEAR(p.position(0).quantity, 0.0, 1e-12);
}

TEST(portfolio_buy_fill) {
    qe::Portfolio p(10000.0);
    qe::Order order;
    order.id = 1;
    order.symbol_id = 0;
    order.side = qe::Side::BUY;
    order.quantity = 1.0;

    // 买入 1 个 @ 100, 手续费率 0.001
    auto fill = p.fill_order(order, 100.0, 1000, 0.001);

    ASSERT_NEAR(fill.price, 100.0, 1e-12);
    ASSERT_NEAR(fill.quantity, 1.0, 1e-12);
    ASSERT_NEAR(fill.commission, 0.1, 1e-12);   // 100 * 0.001
    ASSERT_NEAR(fill.pnl, 0.0, 1e-12);

    ASSERT_NEAR(p.position(0).quantity, 1.0, 1e-12);
    ASSERT_NEAR(p.position(0).avg_entry_price, 100.0, 1e-12);
    ASSERT_NEAR(p.cash(), 10000.0 - 100.0 - 0.1, 1e-12);
}

TEST(portfolio_sell_pnl) {
    qe::Portfolio p(10000.0);

    // 先买
    qe::Order buy_order;
    buy_order.id = 1;
    buy_order.symbol_id = 0;
    buy_order.side = qe::Side::BUY;
    buy_order.quantity = 2.0;
    p.fill_order(buy_order, 100.0, 1000, 0.0);
    // cash = 10000 - 200 = 9800, position = 2 @ 100

    // 卖 1 个 @ 150
    qe::Order sell_order;
    sell_order.id = 2;
    sell_order.symbol_id = 0;
    sell_order.side = qe::Side::SELL;
    sell_order.quantity = 1.0;
    auto fill = p.fill_order(sell_order, 150.0, 2000, 0.0);

    ASSERT_NEAR(fill.pnl, 50.0, 1e-12);  // (150 - 100) * 1
    ASSERT_NEAR(p.position(0).quantity, 1.0, 1e-12);
    ASSERT_NEAR(p.position(0).realized_pnl, 50.0, 1e-12);
    ASSERT_NEAR(p.cash(), 9800.0 + 150.0, 1e-12);
}

TEST(portfolio_sell_all_clears_position) {
    qe::Portfolio p(10000.0);

    qe::Order buy_order;
    buy_order.id = 1;
    buy_order.symbol_id = 0;
    buy_order.side = qe::Side::BUY;
    buy_order.quantity = 1.0;
    p.fill_order(buy_order, 100.0, 1000, 0.0);

    qe::Order sell_order;
    sell_order.id = 2;
    sell_order.symbol_id = 0;
    sell_order.side = qe::Side::SELL;
    sell_order.quantity = 1.0;
    p.fill_order(sell_order, 120.0, 2000, 0.0);

    ASSERT_NEAR(p.position(0).quantity, 0.0, 1e-12);
    ASSERT_NEAR(p.position(0).avg_entry_price, 0.0, 1e-12);
}

TEST(portfolio_update_price_unrealized_pnl) {
    qe::Portfolio p(10000.0);

    qe::Order buy_order;
    buy_order.id = 1;
    buy_order.symbol_id = 0;
    buy_order.side = qe::Side::BUY;
    buy_order.quantity = 2.0;
    p.fill_order(buy_order, 100.0, 1000, 0.0);
    // position: 2 @ 100

    p.update_price(0, 150.0);
    ASSERT_NEAR(p.position(0).unrealized_pnl, 100.0, 1e-12);  // (150-100)*2

    p.recalc_equity();
    // equity = cash(9800) + 2 * 150 = 10100
    ASSERT_NEAR(p.equity(), 10100.0, 1e-12);
}

TEST(portfolio_multi_symbol) {
    qe::Portfolio p(10000.0);

    qe::Order o1;
    o1.id = 1; o1.symbol_id = 0; o1.side = qe::Side::BUY; o1.quantity = 1.0;
    p.fill_order(o1, 100.0, 1000, 0.0);

    qe::Order o2;
    o2.id = 2; o2.symbol_id = 1; o2.side = qe::Side::BUY; o2.quantity = 5.0;
    p.fill_order(o2, 50.0, 1000, 0.0);

    // cash = 10000 - 100 - 250 = 9650
    ASSERT_NEAR(p.cash(), 9650.0, 1e-12);

    p.update_price(0, 100.0);
    p.update_price(1, 50.0);
    p.recalc_equity();
    // equity = 9650 + 100 + 250 = 10000
    ASSERT_NEAR(p.equity(), 10000.0, 1e-12);
}