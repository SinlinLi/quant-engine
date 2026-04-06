#include "strategies/macd_cross.h"
#include "core/engine.h"

namespace qe {

MACDCross::MACDCross(const std::string& symbol, int fast, int slow, int signal)
    : symbol_name_(symbol), fast_(fast), slow_(slow), signal_(signal) {}

void MACDCross::on_init(Context& ctx) {
    sid_ = ctx.symbol(symbol_name_);
    macd_ = &ctx.indicator<MACD>(sid_, fast_, slow_, signal_);
}

void MACDCross::on_bar(Context& ctx, uint16_t symbol_id, const Bar& bar) {
    if (symbol_id != sid_) return;
    if (!macd_->ready()) {
        prev_hist_ = 0.0;
        return;
    }

    double hist = macd_->histogram();
    auto& pos = ctx.position(sid_);

    // 金叉
    if (prev_hist_ <= 0.0 && hist > 0.0 && pos.quantity < 1e-12)
        ctx.buy(sid_, 0.1);

    // 死叉
    if (prev_hist_ >= 0.0 && hist < 0.0 && pos.quantity > 1e-12)
        ctx.sell(sid_, pos.quantity);

    prev_hist_ = hist;
}

void MACDCross::on_stop(Context& ctx) {
    auto& pos = ctx.position(sid_);
    if (pos.quantity > 0)
        ctx.sell(sid_, pos.quantity);
}

}  // namespace qe
