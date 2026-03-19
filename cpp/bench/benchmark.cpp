#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <random>
#include <memory>
#include "core/engine.h"
#include "core/sim_broker.h"
#include "data/csv_feed.h"
#include "core/strategy.h"
#include "indicator/sma.h"
#include "indicator/ema.h"
#include "indicator/rsi.h"
#include "indicator/macd.h"
#include "indicator/bollinger.h"

// ---------- helpers ----------

static std::vector<qe::Bar> generate_bars(size_t count, int64_t start_ts = 1000000,
                                           int64_t interval_ms = 60000) {
    std::mt19937 rng(42);  // deterministic
    std::normal_distribution<double> ret_dist(0.0, 0.002);

    std::vector<qe::Bar> bars;
    bars.reserve(count);

    double price = 50000.0;
    for (size_t i = 0; i < count; ++i) {
        double r = ret_dist(rng);
        double open = price;
        double close = open * (1.0 + r);
        double high = std::max(open, close) * (1.0 + std::abs(ret_dist(rng)) * 0.5);
        double low  = std::min(open, close) * (1.0 - std::abs(ret_dist(rng)) * 0.5);
        double vol  = 100.0 + std::abs(ret_dist(rng)) * 50000.0;
        bars.push_back({start_ts + static_cast<int64_t>(i) * interval_ms,
                        open, high, low, close, vol, vol * close});
        price = close;
    }
    return bars;
}

struct BenchResult {
    const char* name;
    size_t bars;
    int symbols;
    double elapsed_ms;
    double bars_per_sec;
};

static void print_result(const BenchResult& r) {
    printf("  %-44s %8zu bars × %d sym  %8.2f ms  %12.0f bars/s\n",
           r.name, r.bars, r.symbols, r.elapsed_ms, r.bars_per_sec);
}

// ---------- strategies ----------

class BuyAndHold : public qe::Strategy {
public:
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar& bar) override {
        if (ctx.position(sid).quantity < 1e-12)
            ctx.buy(sid, 0.01);
    }
};

class DualSMA : public qe::Strategy {
    int fast_, slow_;
    std::vector<uint16_t> sids_;
public:
    DualSMA(int fast = 10, int slow = 30, std::vector<uint16_t> sids = {})
        : fast_(fast), slow_(slow), sids_(std::move(sids)) {}
    void on_init(qe::Context& ctx) override {
        for (auto sid : sids_) {
            ctx.indicator<qe::SMA>(sid, fast_);
            ctx.indicator<qe::SMA>(sid, slow_);
        }
    }
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar& bar) override {
        auto& f = ctx.indicator<qe::SMA>(sid, fast_);
        auto& s = ctx.indicator<qe::SMA>(sid, slow_);
        if (!f.ready() || !s.ready()) return;
        if (f.value() > s.value() && ctx.position(sid).quantity < 1e-12)
            ctx.buy(sid, 0.01);
        else if (f.value() < s.value() && ctx.position(sid).quantity > 1e-12)
            ctx.sell(sid, ctx.position(sid).quantity);
    }
    void on_stop(qe::Context& ctx) override {}
};

class HeavyIndicator : public qe::Strategy {
    std::vector<uint16_t> sids_;
public:
    explicit HeavyIndicator(std::vector<uint16_t> sids = {}) : sids_(std::move(sids)) {}
    void on_init(qe::Context& ctx) override {
        for (auto sid : sids_) {
            ctx.indicator<qe::SMA>(sid, 20);
            ctx.indicator<qe::SMA>(sid, 50);
            ctx.indicator<qe::EMA>(sid, 12);
            ctx.indicator<qe::EMA>(sid, 26);
            ctx.indicator<qe::RSI>(sid, 14);
            ctx.indicator<qe::MACD>(sid, 12, 26, 9);
            ctx.indicator<qe::Bollinger>(sid, 20, 2.0);
        }
    }
    void on_bar(qe::Context& ctx, uint16_t sid, const qe::Bar& bar) override {
        auto& sma20  = ctx.indicator<qe::SMA>(sid, 20);
        auto& sma50  = ctx.indicator<qe::SMA>(sid, 50);
        auto& ema12  = ctx.indicator<qe::EMA>(sid, 12);
        auto& ema26  = ctx.indicator<qe::EMA>(sid, 26);
        auto& rsi    = ctx.indicator<qe::RSI>(sid, 14);
        auto& macd   = ctx.indicator<qe::MACD>(sid, 12, 26, 9);
        auto& boll   = ctx.indicator<qe::Bollinger>(sid, 20, 2.0);

        if (!sma20.ready() || !rsi.ready() || !macd.ready() || !boll.ready()) return;

        // complex signal: RSI oversold + price below lower band + MACD histogram positive
        bool buy_sig  = rsi.value() < 30 && bar.close < boll.lower() && macd.histogram() > 0;
        bool sell_sig = rsi.value() > 70 && bar.close > boll.upper() && macd.histogram() < 0;

        if (buy_sig && ctx.position(sid).quantity < 1e-12)
            ctx.buy(sid, 0.01);
        else if (sell_sig && ctx.position(sid).quantity > 1e-12)
            ctx.sell(sid, ctx.position(sid).quantity);
    }
    void on_stop(qe::Context& ctx) override {}
};

// ---------- benchmark runner ----------

template<typename StrategyFactory>
static BenchResult run_bench(const char* name, size_t bar_count, int num_symbols,
                             StrategyFactory make_strategy) {
    qe::Engine engine;
    size_t total_bars = 0;
    std::vector<uint16_t> sids;

    for (int i = 0; i < num_symbols; ++i) {
        char sym[16];
        snprintf(sym, sizeof(sym), "SYM%d", i);
        auto sid = engine.symbols().id(sym);
        sids.push_back(sid);
        auto bars = generate_bars(bar_count, 1000000 + i * 100);
        total_bars += bars.size();
        engine.add_feed(std::make_unique<qe::CsvFeed>(sid, std::move(bars)));
    }

    qe::SimBrokerConfig cfg;
    cfg.cash = 1000000.0;
    cfg.commission_rate = 0.0004;
    engine.set_broker(std::make_unique<qe::SimBroker>(cfg));
    engine.add_strategy(make_strategy(sids));

    auto t0 = std::chrono::high_resolution_clock::now();
    engine.run();
    auto t1 = std::chrono::high_resolution_clock::now();

    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return {name, bar_count, num_symbols, ms, total_bars / (ms / 1000.0)};
}

int main() {
    printf("\n  quant-engine benchmark\n");
    printf("  ─────────────────────────────────────────────────────────────────────────────\n");

    std::vector<BenchResult> results;

    // 1. Throughput scaling: buy-and-hold, 1 symbol, increasing bar count
    for (size_t n : {10000, 100000, 500000, 1000000}) {
        results.push_back(run_bench("buy_and_hold", n, 1,
            [](const std::vector<uint16_t>&){ return std::make_shared<BuyAndHold>(); }));
    }

    printf("\n  [1] Throughput scaling (BuyAndHold, 1 symbol)\n");
    for (auto& r : results) print_result(r);

    results.clear();

    // 2. Multi-symbol scaling: DualSMA, 100k bars, increasing symbols
    for (int s : {1, 4, 16, 64}) {
        results.push_back(run_bench("dual_sma_multi", 100000, s,
            [](const std::vector<uint16_t>& sids){ return std::make_shared<DualSMA>(10, 30, sids); }));
    }

    printf("\n  [2] Multi-symbol scaling (DualSMA 10/30, 100K bars)\n");
    for (auto& r : results) print_result(r);

    results.clear();

    // 3. Indicator overhead: heavy indicators (7 indicators per symbol), 1 symbol
    for (size_t n : {10000, 100000, 500000}) {
        results.push_back(run_bench("heavy_indicators", n, 1,
            [](const std::vector<uint16_t>& sids){ return std::make_shared<HeavyIndicator>(sids); }));
    }

    printf("\n  [3] Indicator overhead (7 indicators, 1 symbol)\n");
    for (auto& r : results) print_result(r);

    results.clear();

    // 4. Stress test: heavy indicators × multi-symbol
    for (int s : {1, 4, 16}) {
        results.push_back(run_bench("stress", 100000, s,
            [](const std::vector<uint16_t>& sids){ return std::make_shared<HeavyIndicator>(sids); }));
    }

    printf("\n  [4] Stress test (7 indicators, 100K bars, multi-symbol)\n");
    for (auto& r : results) print_result(r);

    printf("\n  ─────────────────────────────────────────────────────────────────────────────\n\n");
    return 0;
}
