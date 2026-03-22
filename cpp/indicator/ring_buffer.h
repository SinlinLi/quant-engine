#pragma once

#include <cstddef>
#include <vector>

namespace qe {

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : data_(capacity, 0.0), capacity_(capacity) {}

    void push(double val) {
        if (count_ == capacity_)
            kahan_sub(data_[head_]);  // 移除即将被覆盖的旧值
        data_[head_] = val;
        kahan_add(val);
        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_) ++count_;
    }

    // 0 = 最新，1 = 前一个，...
    double operator[](size_t ago) const {
        size_t idx = (head_ + capacity_ - 1 - ago) % capacity_;
        return data_[idx];
    }

    size_t count() const { return count_; }
    size_t capacity() const { return capacity_; }
    bool full() const { return count_ == capacity_; }

    double sum() const { return running_sum_; }

private:
    std::vector<double> data_;
    size_t capacity_;
    size_t head_ = 0;
    size_t count_ = 0;
    double running_sum_ = 0.0;
    double kahan_comp_ = 0.0;  // Kahan 补偿项

    void kahan_add(double val) {
        double y = val - kahan_comp_;
        double t = running_sum_ + y;
        kahan_comp_ = (t - running_sum_) - y;
        running_sum_ = t;
    }

    void kahan_sub(double val) {
        kahan_add(-val);
    }
};

}  // namespace qe
