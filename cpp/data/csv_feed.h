#pragma once

#include <string>
#include <vector>
#include "data/data_feed.h"

namespace qe {

// 从 Binance 格式 CSV 加载 K 线数据
// CSV 列: open_time,open,high,low,close,volume,close_time,quote_volume,...
class CsvFeed : public BarFeed {
public:
    CsvFeed(uint16_t symbol_id, const std::string& csv_path);

    // 从已有 vector 构造（用于测试或从 ClickHouse 加载后传入）
    CsvFeed(uint16_t symbol_id, std::vector<Bar> bars);

    uint16_t symbol_id() const override { return symbol_id_; }
    bool next() override;
    int64_t timestamp() const override;
    const Bar& current_bar() const override;

    size_t size() const { return bars_.size(); }

private:
    uint16_t symbol_id_;
    std::vector<Bar> bars_;
    size_t index_ = 0;
    bool started_ = false;

    void load_csv(const std::string& path);
};

}  // namespace qe
