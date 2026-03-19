#include "strategies/dual_ma.h"
#include "core/engine.h"

namespace qe {

DualMA::DualMA(const std::string& symbol, int fast_period, int slow_period)
    : symbol_name_(symbol), fast_period_(fast_period), slow_period_(slow_period) {}

void DualMA::on_init(Context& ctx) {
    sid_ = ctx.symbol(symbol_name_);
    fast_ = &ctx.indicator<SMA>(sid_, fast_period_);
    slow_ = &ctx.indicator<SMA>(sid_, slow_period_);
}

void DualMA::on_bar(Context& ctx, uint16_t symbol_id, const Bar& bar) {
    if (symbol_id != sid_) return;
    if (!fast_->ready() || !slow_->ready()) return;

    auto& pos = ctx.position(sid_);

    if (fast_->value() > slow_->value() && pos.quantity == 0.0)
        ctx.buy(sid_, 0.1);
    else if (fast_->value() < slow_->value() && pos.quantity > 0.0)
        ctx.sell(sid_, pos.quantity);
}

void DualMA::on_stop(Context& ctx) {
    auto& pos = ctx.position(sid_);
    if (pos.quantity > 0)
        ctx.sell(sid_, pos.quantity);
}

}  // namespace qe
