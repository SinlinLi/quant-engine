#pragma once

#include <memory>
#include <vector>
#include "core/broker.h"
#include "core/context.h"
#include "core/strategy.h"
#include "core/symbol_table.h"
#include "data/data_feed.h"
#include "indicator/indicator.h"

namespace qe {

struct PerformanceResult {
    double sharpe = 0.0;
    double max_drawdown = 0.0;
    double annual_return = 0.0;
    double total_return = 0.0;
    uint32_t total_trades = 0;
    double win_rate = 0.0;
    double initial_cash = 0.0;
    double final_equity = 0.0;
    std::vector<double> equity_curve;
};

class Engine {
public:
    Engine();

    void add_feed(std::unique_ptr<BarFeed> feed);
    void set_broker(std::unique_ptr<Broker> broker);
    void add_strategy(std::shared_ptr<Strategy> strategy);

    SymbolTable& symbols() { return symbols_; }

    // 注册指标（由 Context::indicator 调用）
    void register_indicator(uint16_t symbol_id, std::unique_ptr<Indicator> ind);

    PerformanceResult run();

private:
    SymbolTable symbols_;
    std::vector<std::unique_ptr<BarFeed>> feeds_;
    std::unique_ptr<Broker> broker_;
    std::vector<std::shared_ptr<Strategy>> strategies_;

    // indicators_[symbol_id] = vector of indicators for that symbol
    std::vector<std::vector<Indicator*>> indicators_;
    std::vector<std::unique_ptr<Indicator>> indicator_storage_;
};

}  // namespace qe

// 模板实现必须在头文件中
#include "core/context_impl.h"
