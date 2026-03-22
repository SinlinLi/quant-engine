#include "analyzer/performance.h"
#include "core/engine.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace qe {

void calc_performance(const std::vector<FillEvent>& fills,
                      const std::vector<double>& equity_curve,
                      double initial_cash,
                      int64_t start_ms, int64_t end_ms,
                      PerformanceResult& result) {
    // 统计交易
    int wins = 0;
    int sell_count = 0;
    for (const auto& f : fills) {
        if (f.side == Side::SELL) {
            ++sell_count;
            if (f.pnl - f.commission - f.buy_commission > 0) ++wins;
        }
    }
    result.total_trades = sell_count;
    result.win_rate = sell_count > 0 ? static_cast<double>(wins) / sell_count : 0.0;

    if (equity_curve.size() < 2) return;

    // 计算收益率序列
    std::vector<double> returns;
    returns.reserve(equity_curve.size() - 1);
    for (size_t i = 1; i < equity_curve.size(); ++i) {
        if (equity_curve[i - 1] < 1e-12) continue;  // 跳过 equity=0 避免除零
        double r = (equity_curve[i] - equity_curve[i - 1]) / equity_curve[i - 1];
        returns.push_back(r);
    }

    // 最大回撤
    double peak = initial_cash;
    double max_dd = 0.0;
    for (double eq : equity_curve) {
        peak = std::max(peak, eq);
        double dd = (peak - eq) / peak;
        max_dd = std::max(max_dd, dd);
    }
    result.max_drawdown = max_dd;

    // 夏普比率 + 年化收益（用实际时间跨度）
    if (returns.empty()) return;

    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double sq_sum = 0.0;
    for (double r : returns)
        sq_sum += (r - mean) * (r - mean);
    double stddev = std::sqrt(sq_sum / returns.size());

    // 用实际时间跨度计算年化因子
    double duration_ms = static_cast<double>(end_ms - start_ms);
    constexpr double MS_PER_YEAR = 365.25 * 24.0 * 3600.0 * 1000.0;
    double samples_per_year = (duration_ms > 0 && returns.size() > 0)
        ? returns.size() * (MS_PER_YEAR / duration_ms)
        : 525600.0;  // fallback

    // 复合年化收益
    double total = result.final_equity / result.initial_cash;
    double years = duration_ms / MS_PER_YEAR;
    result.annual_return = (years > 1e-12) ? std::pow(total, 1.0 / years) - 1.0 : 0.0;
    result.sharpe = stddev > 1e-12 ? (mean / stddev) * std::sqrt(samples_per_year) : 0.0;
}

}  // namespace qe
