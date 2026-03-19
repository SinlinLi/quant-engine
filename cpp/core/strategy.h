#pragma once

#include <cstdint>
#include "core/order.h"
#include "data/bar.h"
#include "data/tick.h"

namespace qe {

class Context;

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void on_init(Context& ctx) {}
    virtual void on_bar(Context& ctx, uint16_t symbol_id, const Bar& bar) = 0;
    virtual void on_tick(Context& ctx, uint16_t symbol_id, const Trade& trade) {}
    virtual void on_order(Context& ctx, const Order& order) {}
    virtual void on_stop(Context& ctx) {}
};

}  // namespace qe
