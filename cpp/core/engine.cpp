#include "core/engine.h"
#include "analyzer/performance.h"
#include <queue>
#include <functional>
#include <stdexcept>
#include <map>

namespace qe {

Engine::Engine() {}

void Engine::add_feed(std::unique_ptr<BarFeed> feed) {
    feeds_.push_back(std::move(feed));
}

void Engine::set_broker(std::unique_ptr<Broker> broker) {
    broker_ = std::move(broker);
}

void Engine::add_strategy(std::shared_ptr<Strategy> strategy) {
    strategies_.push_back(std::move(strategy));
}

void Engine::register_indicator(uint16_t symbol_id, std::unique_ptr<Indicator> ind) {
    if (symbol_id >= indicators_.size())
        indicators_.resize(symbol_id + 1);
    indicators_[symbol_id].push_back(ind.get());
    indicator_storage_.push_back(std::move(ind));
}

PerformanceResult Engine::run() {
    if (!broker_)
        throw std::logic_error("broker not set before run()");

    Context ctx(*this, *broker_, symbols_);

    // 接通 on_order 回调：Broker 成交/取消 → Strategy::on_order()
    broker_->set_order_callback([&](const Order& order) {
        for (auto& strategy : strategies_)
            strategy->on_order(ctx, order);
    });

    // 初始化策略
    for (auto& strategy : strategies_)
        strategy->on_init(ctx);

    // on_init 结束，锁定指标注册
    ctx.lock_init();

    // 小顶堆：按 timestamp 排序
    // feeds_ 存储 BarFeed*，pq 存储 DataFeed*（安全：所有元素均来自 feeds_）
    auto cmp = [](DataFeed* a, DataFeed* b) {
        return a->timestamp() > b->timestamp();
    };
    std::priority_queue<DataFeed*, std::vector<DataFeed*>, decltype(cmp)> pq(cmp);

    // 每个 feed 读第一条
    for (auto& feed : feeds_) {
        if (feed->next())
            pq.push(feed.get());
    }

    double initial_equity = broker_->equity();
    PerformanceResult result;
    result.initial_cash = initial_equity;

    // 记录每个 symbol 的最后一根 bar（on_stop flush 用）
    // 用 std::map 保证 flush 遍历顺序确定（按 symbol_id 排序）
    std::map<uint16_t, Bar> last_bars;
    int64_t first_ts = 0;
    int64_t last_ts = 0;

    // 主循环
    while (!pq.empty()) {
        auto* feed = pq.top();
        pq.pop();

        auto sid = feed->symbol_id();
        auto& bar = static_cast<BarFeed*>(feed)->current_bar();

        ctx.set_current_time(bar.timestamp_ms);
        if (first_ts == 0) first_ts = bar.timestamp_ms;
        last_ts = bar.timestamp_ms;
        last_bars[sid] = bar;

        // 1. 更新指标
        if (sid < indicators_.size()) {
            for (auto* ind : indicators_[sid])
                ind->update(bar);
        }

        // 2. 撮合挂单
        broker_->on_bar(sid, bar);

        // 3. 通知策略
        for (auto& strategy : strategies_)
            strategy->on_bar(ctx, sid, bar);

        // 4. 逐 bar 采样净值曲线
        result.equity_curve.push_back(broker_->equity());

        // 5. 推进 feed
        if (feed->next())
            pq.push(feed);
    }

    // 清除主循环残留的 pending 订单（数据已耗尽，无法正常成交）
    broker_->cancel_all_pending();

    // 通知策略结束（策略可在此提交平仓订单）
    for (auto& strategy : strategies_)
        strategy->on_stop(ctx);

    // flush: 用 close 价撮合 on_stop 阶段新提交的订单
    // close 是该 bar 最后已知价格，语义上对应"收盘后平仓"
    for (auto& [sid, bar] : last_bars) {
        Bar flush_bar = bar;
        flush_bar.open = bar.close;   // 市价单以 close 价成交
        flush_bar.high = bar.close;
        flush_bar.low = bar.close;
        broker_->on_bar(sid, flush_bar);
    }

    // flush 后追加最终 equity 采样点
    result.equity_curve.push_back(broker_->equity());

    // 计算绩效
    result.final_equity = broker_->equity();
    result.total_return = (result.final_equity - result.initial_cash) / result.initial_cash;

    const auto& fills = broker_->fills();
    calc_performance(fills, result.equity_curve, result.initial_cash, first_ts, last_ts, result);
    result.fills = fills;

    return result;
}

}  // namespace qe
