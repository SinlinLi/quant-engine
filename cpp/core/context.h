#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include "core/order.h"
#include "indicator/indicator.h"

namespace qe {

class Broker;
class SymbolTable;
class Engine;

class Context {
public:
    Context(Engine& engine, Broker& broker, SymbolTable& symbols);

    // 下单
    uint64_t buy(uint16_t symbol_id, double quantity);
    uint64_t sell(uint16_t symbol_id, double quantity);
    uint64_t buy_limit(uint16_t symbol_id, double price, double quantity);
    uint64_t sell_limit(uint16_t symbol_id, double price, double quantity);
    uint64_t stop_loss(uint16_t symbol_id, double stop_price, double quantity);
    uint64_t stop_limit(uint16_t symbol_id, double stop_price, double limit_price, double quantity);
    bool cancel(uint64_t order_id);

    // 查询
    const Position& position(uint16_t symbol_id) const;
    double equity() const;
    double cash() const;

    // Symbol 查找
    uint16_t symbol(const std::string& name) const;
    const std::string& symbol_name(uint16_t id) const;

    // 指标注册（模板实现在 context_impl.h）
    // on_init 阶段创建，之后重复调用返回缓存实例
    template<typename T, typename... Args>
    T& indicator(uint16_t symbol_id, Args&&... args);

    // on_init 结束后锁定，禁止创建新指标
    void lock_init() { init_done_ = true; }

    // 当前时间戳
    int64_t current_time() const { return current_time_; }
    void set_current_time(int64_t ts) { current_time_ = ts; }

private:
    Engine& engine_;
    Broker& broker_;
    SymbolTable& symbols_;
    int64_t current_time_ = 0;
    bool init_done_ = false;
    std::unordered_map<std::string, Indicator*> indicator_cache_;
};

}  // namespace qe
