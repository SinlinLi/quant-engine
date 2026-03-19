#pragma once

#include <cstdint>

namespace qe {

enum class Side : uint8_t { BUY, SELL };
enum class OrderType : uint8_t { MARKET, LIMIT };
enum class OrderStatus : uint8_t { PENDING, FILLED, PARTIALLY_FILLED, CANCELLED };

struct Order {
    uint64_t id = 0;
    uint16_t symbol_id = 0;
    Side side = Side::BUY;
    OrderType type = OrderType::MARKET;
    double price = 0.0;
    double quantity = 0.0;
    double filled_quantity = 0.0;
    double commission = 0.0;
    OrderStatus status = OrderStatus::PENDING;
    int64_t created_at = 0;
    int64_t filled_at = 0;
};

struct Position {
    double quantity = 0.0;
    double avg_entry_price = 0.0;
    double unrealized_pnl = 0.0;
    double realized_pnl = 0.0;
};

struct FillEvent {
    uint64_t order_id;
    uint16_t symbol_id;
    Side side;
    double price;
    double quantity;
    double commission;
    double pnl;
    int64_t timestamp_ms;
};

}  // namespace qe
