#include "test/test_framework.h"
#include "core/sim_broker.h"

static qe::SimBroker make_broker(double cash = 10000.0, double commission = 0.0,
                                  double slippage = 0.0) {
    qe::SimBrokerConfig cfg;
    cfg.cash = cash;
    cfg.commission_rate = commission;
    cfg.slippage = slippage;
    return qe::SimBroker(cfg);
}

// 市价买单: 提交后在下一根 bar 的 on_bar 时以 open 成交
TEST(broker_market_buy) {
    auto broker = make_broker(10000.0);

    qe::Order order;
    order.symbol_id = 0;
    order.side = qe::Side::BUY;
    order.type = qe::OrderType::MARKET;
    order.quantity = 1.0;
    auto id = broker.submit_order(order);
    ASSERT_GT(id, (uint64_t)0);

    // 下一根 bar: open=100
    auto bar = make_bar(1000, 100, 110, 90, 105);
    broker.on_bar(0, bar);

    ASSERT_NEAR(broker.position(0).quantity, 1.0, 1e-12);
    ASSERT_EQ(broker.fills().size(), (size_t)1);
    ASSERT_NEAR(broker.fills()[0].price, 100.0, 1e-12);  // fill at open
}

// 市价卖单
TEST(broker_market_sell) {
    auto broker = make_broker(10000.0);

    // 先买入
    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 2.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));
    // position: 2 @ 100

    // 卖出
    qe::Order sell;
    sell.symbol_id = 0; sell.side = qe::Side::SELL;
    sell.type = qe::OrderType::MARKET; sell.quantity = 2.0;
    broker.submit_order(sell);
    broker.on_bar(0, make_bar(2000, 120, 130, 115, 125));
    // fill at open=120

    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
    ASSERT_EQ(broker.fills().size(), (size_t)2);
    ASSERT_NEAR(broker.fills()[1].price, 120.0, 1e-12);
    ASSERT_NEAR(broker.fills()[1].pnl, 40.0, 1e-12);  // (120-100)*2
}

// 限价买单: 只有 bar.low <= limit_price 才成交
TEST(broker_limit_buy_fill) {
    auto broker = make_broker(10000.0);

    qe::Order order;
    order.symbol_id = 0; order.side = qe::Side::BUY;
    order.type = qe::OrderType::LIMIT;
    order.price = 95.0; order.quantity = 1.0;
    broker.submit_order(order);

    // bar1: low=96 → 不成交
    broker.on_bar(0, make_bar(1000, 100, 102, 96, 99));
    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);

    // bar2: low=93 → 成交 @ 95
    broker.on_bar(0, make_bar(2000, 98, 101, 93, 97));
    ASSERT_NEAR(broker.position(0).quantity, 1.0, 1e-12);
    ASSERT_NEAR(broker.fills().back().price, 95.0, 1e-12);
}

// 限价卖单: 只有 bar.high >= limit_price 才成交
TEST(broker_limit_sell_fill) {
    auto broker = make_broker(10000.0);

    // 先持仓
    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    // 限价卖 @ 115
    qe::Order sell;
    sell.symbol_id = 0; sell.side = qe::Side::SELL;
    sell.type = qe::OrderType::LIMIT;
    sell.price = 115.0; sell.quantity = 1.0;
    broker.submit_order(sell);

    // bar: high=113 → 不成交
    broker.on_bar(0, make_bar(2000, 108, 113, 106, 110));
    ASSERT_NEAR(broker.position(0).quantity, 1.0, 1e-12);

    // bar: high=118 → 成交 @ 115
    broker.on_bar(0, make_bar(3000, 112, 118, 111, 116));
    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
    ASSERT_NEAR(broker.fills().back().price, 115.0, 1e-12);
}

// 撤单
TEST(broker_cancel_order) {
    auto broker = make_broker(10000.0);

    qe::Order order;
    order.symbol_id = 0; order.side = qe::Side::BUY;
    order.type = qe::OrderType::LIMIT;
    order.price = 90.0; order.quantity = 1.0;
    auto id = broker.submit_order(order);

    ASSERT_TRUE(broker.cancel_order(id));
    ASSERT_FALSE(broker.cancel_order(id));  // 已撤，再撤失败

    // 即使价格触及也不会成交
    broker.on_bar(0, make_bar(1000, 85, 91, 80, 88));
    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
}

// 滑点
TEST(broker_slippage) {
    auto broker = make_broker(10000.0, 0.0, 0.01);  // 1% 滑点

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);

    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));
    // fill_price = 100 * (1 + 0.01) = 101
    ASSERT_NEAR(broker.fills().back().price, 101.0, 1e-12);
}

// 手续费
TEST(broker_commission) {
    auto broker = make_broker(10000.0, 0.001, 0.0);  // 0.1%

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);

    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));
    // commission = 100 * 1 * 0.001 = 0.1
    ASSERT_NEAR(broker.fills().back().commission, 0.1, 1e-12);
    ASSERT_NEAR(broker.available_cash(), 10000.0 - 100.0 - 0.1, 1e-12);
}

// 不同 symbol 的订单互不干扰
TEST(broker_cross_symbol_isolation) {
    auto broker = make_broker(10000.0);

    qe::Order o1;
    o1.symbol_id = 0; o1.side = qe::Side::BUY;
    o1.type = qe::OrderType::MARKET; o1.quantity = 1.0;
    broker.submit_order(o1);

    qe::Order o2;
    o2.symbol_id = 1; o2.side = qe::Side::BUY;
    o2.type = qe::OrderType::MARKET; o2.quantity = 3.0;
    broker.submit_order(o2);

    // only symbol 0 bar → only symbol 0 order fills
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));
    ASSERT_NEAR(broker.position(0).quantity, 1.0, 1e-12);
    ASSERT_NEAR(broker.position(1).quantity, 0.0, 1e-12);

    broker.on_bar(1, make_bar(1000, 50, 55, 45, 52));
    ASSERT_NEAR(broker.position(1).quantity, 3.0, 1e-12);
}

// 超卖防护: 卖出量 > 持仓 → clamp 到实际持仓
TEST(broker_oversell_clamp) {
    auto broker = make_broker(10000.0);

    // 买 1.0
    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    // 卖 5.0 — 应该 clamp 到 1.0
    qe::Order sell;
    sell.symbol_id = 0; sell.side = qe::Side::SELL;
    sell.type = qe::OrderType::MARKET; sell.quantity = 5.0;
    broker.submit_order(sell);
    broker.on_bar(0, make_bar(2000, 120, 130, 115, 125));

    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
    ASSERT_NEAR(broker.fills().back().quantity, 1.0, 1e-12);  // clamped
}

// 无持仓卖出 → 拒绝
TEST(broker_sell_no_position) {
    auto broker = make_broker(10000.0);

    qe::Order sell;
    sell.symbol_id = 0; sell.side = qe::Side::SELL;
    sell.type = qe::OrderType::MARKET; sell.quantity = 1.0;
    broker.submit_order(sell);
    broker.on_bar(0, make_bar(1000, 100, 110, 90, 105));

    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
    ASSERT_EQ(broker.fills().size(), (size_t)0);  // 没有成交
}

// 资金不足 → 拒绝买入
TEST(broker_insufficient_funds) {
    auto broker = make_broker(100.0);  // 只有 100 块

    qe::Order buy;
    buy.symbol_id = 0; buy.side = qe::Side::BUY;
    buy.type = qe::OrderType::MARKET; buy.quantity = 1.0;
    broker.submit_order(buy);

    // open=200，需要 200 > 100，应该拒绝
    broker.on_bar(0, make_bar(1000, 200, 210, 190, 205));

    ASSERT_NEAR(broker.position(0).quantity, 0.0, 1e-12);
    ASSERT_EQ(broker.fills().size(), (size_t)0);
    ASSERT_NEAR(broker.available_cash(), 100.0, 1e-12);  // 资金不变
}