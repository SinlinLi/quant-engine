#include "core/context.h"
#include "core/broker.h"
#include "core/symbol_table.h"

namespace qe {

Context::Context(Engine& engine, Broker& broker, SymbolTable& symbols)
    : engine_(engine), broker_(broker), symbols_(symbols) {}

uint64_t Context::buy(uint16_t symbol_id, double quantity) {
    Order order;
    order.symbol_id = symbol_id;
    order.side = Side::BUY;
    order.type = OrderType::MARKET;
    order.quantity = quantity;
    order.created_at = current_time_;
    return broker_.submit_order(order);
}

uint64_t Context::sell(uint16_t symbol_id, double quantity) {
    Order order;
    order.symbol_id = symbol_id;
    order.side = Side::SELL;
    order.type = OrderType::MARKET;
    order.quantity = quantity;
    order.created_at = current_time_;
    return broker_.submit_order(order);
}

uint64_t Context::buy_limit(uint16_t symbol_id, double price, double quantity) {
    Order order;
    order.symbol_id = symbol_id;
    order.side = Side::BUY;
    order.type = OrderType::LIMIT;
    order.price = price;
    order.quantity = quantity;
    order.created_at = current_time_;
    return broker_.submit_order(order);
}

uint64_t Context::sell_limit(uint16_t symbol_id, double price, double quantity) {
    Order order;
    order.symbol_id = symbol_id;
    order.side = Side::SELL;
    order.type = OrderType::LIMIT;
    order.price = price;
    order.quantity = quantity;
    order.created_at = current_time_;
    return broker_.submit_order(order);
}

uint64_t Context::stop_loss(uint16_t symbol_id, double stop_price, double quantity) {
    Order order;
    order.symbol_id = symbol_id;
    order.side = Side::SELL;
    order.type = OrderType::STOP_MARKET;
    order.stop_price = stop_price;
    order.quantity = quantity;
    order.created_at = current_time_;
    return broker_.submit_order(order);
}

uint64_t Context::stop_limit(uint16_t symbol_id, double stop_price,
                              double limit_price, double quantity) {
    Order order;
    order.symbol_id = symbol_id;
    order.side = Side::SELL;
    order.type = OrderType::STOP_LIMIT;
    order.stop_price = stop_price;
    order.price = limit_price;
    order.quantity = quantity;
    order.created_at = current_time_;
    return broker_.submit_order(order);
}

bool Context::cancel(uint64_t order_id) {
    return broker_.cancel_order(order_id);
}

const Position& Context::position(uint16_t symbol_id) const {
    return broker_.position(symbol_id);
}

double Context::equity() const {
    return broker_.equity();
}

double Context::cash() const {
    return broker_.available_cash();
}

uint16_t Context::symbol(const std::string& name) const {
    return symbols_.find(name);
}

const std::string& Context::symbol_name(uint16_t id) const {
    return symbols_.name(id);
}

}  // namespace qe
