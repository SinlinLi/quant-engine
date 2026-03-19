#pragma once

// Context::indicator 的模板实现
// 必须在 Engine 完整定义之后 include

#include "core/context.h"
#include "core/engine.h"
#include <stdexcept>
#include <typeinfo>

namespace qe {

namespace detail {
template<typename T>
void append_key(std::string& key, const T& v) {
    key += ':';
    if constexpr (std::is_integral_v<T>)
        key += std::to_string(v);
    else if constexpr (std::is_floating_point_v<T>)
        key += std::to_string(v);
    else
        key += "?";
}
}  // namespace detail

template<typename T, typename... Args>
T& Context::indicator(uint16_t symbol_id, Args&&... args) {
    // 缓存 key: "type:sid:arg0:arg1:..."
    std::string key = std::string(typeid(T).name()) + ':' + std::to_string(symbol_id);
    (detail::append_key(key, args), ...);

    auto it = indicator_cache_.find(key);
    if (it != indicator_cache_.end())
        return static_cast<T&>(*it->second);

    // on_init 结束后禁止创建新指标
    if (init_done_)
        throw std::logic_error("indicator() called after on_init");

    auto ind = std::make_unique<T>(std::forward<Args>(args)...);
    T& ref = *ind;
    engine_.register_indicator(symbol_id, std::move(ind));
    indicator_cache_[key] = &ref;
    return ref;
}

}  // namespace qe
