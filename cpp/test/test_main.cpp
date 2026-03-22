#include "test/test_framework.h"

// test_symbol_table.cpp
#include "test/test_symbol_table.cpp"

// test_ring_buffer.cpp
#include "test/test_ring_buffer.cpp"

// test_indicators.cpp
#include "test/test_indicators.cpp"

// test_csv_feed.cpp
#include "test/test_csv_feed.cpp"

// test_portfolio.cpp
#include "test/test_portfolio.cpp"

// test_sim_broker.cpp
#include "test/test_sim_broker.cpp"

// test_rsi.cpp
#include "test/test_rsi.cpp"

// test_macd.cpp
#include "test/test_macd.cpp"

// test_bollinger.cpp
#include "test/test_bollinger.cpp"

// test_engine.cpp
#include "test/test_engine.cpp"

// test_correctness.cpp — P0 正确性测试
#include "test/test_correctness.cpp"

// test_phase2.cpp — Phase 2.2 测试
#include "test/test_phase2.cpp"

int main() {
    printf("=== quant-engine unit tests ===\n\n");

    printf("[SymbolTable]\n");
    RUN_TEST(symbol_id_allocation);
    RUN_TEST(symbol_id_idempotent);
    RUN_TEST(symbol_name_lookup);
    RUN_TEST(symbol_name_out_of_range);
    RUN_TEST(symbol_contains);

    printf("\n[RingBuffer]\n");
    RUN_TEST(ring_buffer_basic);
    RUN_TEST(ring_buffer_wrap);
    RUN_TEST(ring_buffer_sum);

    printf("\n[Indicators]\n");
    RUN_TEST(sma_basic);
    RUN_TEST(sma_period_1);
    RUN_TEST(ema_basic);
    RUN_TEST(ema_period_1);

    printf("\n[CsvFeed]\n");
    RUN_TEST(csv_feed_iteration);
    RUN_TEST(csv_feed_empty);
    RUN_TEST(csv_feed_single_bar);

    printf("\n[Portfolio]\n");
    RUN_TEST(portfolio_init);
    RUN_TEST(portfolio_buy_fill);
    RUN_TEST(portfolio_sell_pnl);
    RUN_TEST(portfolio_sell_all_clears_position);
    RUN_TEST(portfolio_update_price_unrealized_pnl);
    RUN_TEST(portfolio_multi_symbol);

    printf("\n[SimBroker]\n");
    RUN_TEST(broker_market_buy);
    RUN_TEST(broker_market_sell);
    RUN_TEST(broker_limit_buy_fill);
    RUN_TEST(broker_limit_sell_fill);
    RUN_TEST(broker_cancel_order);
    RUN_TEST(broker_slippage);
    RUN_TEST(broker_commission);
    RUN_TEST(broker_cross_symbol_isolation);
    RUN_TEST(broker_oversell_clamp);
    RUN_TEST(broker_sell_no_position);
    RUN_TEST(broker_insufficient_funds);

    printf("\n[RSI]\n");
    RUN_TEST(rsi_all_up);
    RUN_TEST(rsi_all_down);
    RUN_TEST(rsi_mixed);
    RUN_TEST(rsi_not_ready);

    printf("\n[MACD]\n");
    RUN_TEST(macd_not_ready_early);
    RUN_TEST(macd_ready_timing);
    RUN_TEST(macd_constant_price);
    RUN_TEST(macd_uptrend);

    printf("\n[Bollinger]\n");
    RUN_TEST(bollinger_constant_price);
    RUN_TEST(bollinger_basic);
    RUN_TEST(bollinger_not_ready);

    printf("\n[Engine]\n");
    RUN_TEST(engine_empty_feed);
    RUN_TEST(engine_buy_and_hold);
    RUN_TEST(engine_multi_feed_merge);
    RUN_TEST(engine_indicator_lifecycle);
    RUN_TEST(engine_performance_drawdown);

    printf("\n[Correctness]\n");
    RUN_TEST(on_stop_fills_at_close_price);
    RUN_TEST(accounting_identity);
    RUN_TEST(equity_curve_length);
    RUN_TEST(on_order_callback_fired);
    RUN_TEST(submit_zero_quantity_rejected);
    RUN_TEST(win_rate_deducts_both_commissions);
    RUN_TEST(maker_taker_fee_split);
    RUN_TEST(zombie_market_order_cancelled);
    RUN_TEST(sell_slippage);
    RUN_TEST(multiple_buys_avg_price);

    printf("\n[StopOrders]\n");
    RUN_TEST(stop_market_sell_triggered);
    RUN_TEST(stop_market_sell_with_slippage);
    RUN_TEST(stop_market_not_triggered);
    RUN_TEST(stop_limit_sell_triggered);
    RUN_TEST(stop_loss_integration);

    printf("\n[VolumeLimits]\n");
    RUN_TEST(volume_participation_limit);
    RUN_TEST(volume_no_limit_when_zero);

    printf("\n[Performance]\n");
    RUN_TEST(performance_sortino_calmar_profit_factor);

    TEST_SUMMARY();
    return g_tests_failed > 0 ? 1 : 0;
}