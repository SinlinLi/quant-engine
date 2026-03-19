#include "data/csv_feed.h"
#include <cassert>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace qe {

CsvFeed::CsvFeed(uint16_t symbol_id, const std::string& csv_path)
    : symbol_id_(symbol_id) {
    load_csv(csv_path);
}

CsvFeed::CsvFeed(uint16_t symbol_id, std::vector<Bar> bars)
    : symbol_id_(symbol_id), bars_(std::move(bars)) {}

bool CsvFeed::next() {
    if (!started_) {
        started_ = true;
        return !bars_.empty();
    }
    ++index_;
    return index_ < bars_.size();
}

int64_t CsvFeed::timestamp() const {
    assert(index_ < bars_.size());
    return bars_[index_].timestamp_ms;
}

const Bar& CsvFeed::current_bar() const {
    assert(index_ < bars_.size());
    return bars_[index_];
}

void CsvFeed::load_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("cannot open csv: " + path);

    std::string line;
    // 跳过表头（如果第一个字符不是数字）
    if (std::getline(file, line)) {
        if (!line.empty() && (line[0] < '0' || line[0] > '9')) {
            // 是表头，跳过
        } else {
            // 不是表头，回退解析
            file.seekg(0);
        }
    }

    while (std::getline(file, line)) {
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string token;
        Bar bar{};

        // Binance kline CSV: open_time,open,high,low,close,volume,close_time,quote_volume,...
        std::getline(ss, token, ','); bar.timestamp_ms = std::stoll(token);
        std::getline(ss, token, ','); bar.open = std::stod(token);
        std::getline(ss, token, ','); bar.high = std::stod(token);
        std::getline(ss, token, ','); bar.low = std::stod(token);
        std::getline(ss, token, ','); bar.close = std::stod(token);
        std::getline(ss, token, ','); bar.volume = std::stod(token);
        std::getline(ss, token, ','); // close_time, skip
        std::getline(ss, token, ','); bar.quote_volume = std::stod(token);

        bars_.push_back(bar);
    }
}

}  // namespace qe
