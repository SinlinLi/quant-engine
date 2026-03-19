#pragma once

#include <vector>
#include "core/order.h"

namespace qe {

struct PerformanceResult;

void calc_performance(const std::vector<FillEvent>& fills,
                      const std::vector<double>& equity_curve,
                      double initial_cash,
                      int64_t start_ms, int64_t end_ms,
                      PerformanceResult& result);

}  // namespace qe
