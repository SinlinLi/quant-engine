#pragma once

#include <cstdint>
#include <memory>
#include "data/bar.h"
#include "data/tick.h"

namespace qe {

enum class FeedType : uint8_t { BAR, TRADE, DEPTH };

class DataFeed {
public:
    virtual ~DataFeed() = default;
    virtual uint16_t symbol_id() const = 0;
    // 回测: 移动下标，数据用完返回 false（瞬间返回）
    // 实盘: 阻塞等 WebSocket 数据，收到后返回 true
    virtual bool next() = 0;
    virtual int64_t timestamp() const = 0;
    virtual FeedType type() const = 0;
};

class BarFeed : public DataFeed {
public:
    FeedType type() const override { return FeedType::BAR; }
    virtual const Bar& current_bar() const = 0;
};

}  // namespace qe
