#pragma once
#include <cstddef>
#include <limits>
#include <algorithm>
#include <mutex>

class StatCounter {
    double sum;
    size_t n_v;
    double min_v;
    double max_v;
public:
    StatCounter() { reset(); }
    void add(double v) {
        sum += v;
        ++n_v;
        max_v = std::max(max_v, v);
        min_v = std::min(min_v, v);
    }
    void reset() {
        sum = 0;
        n_v = 0;
        min_v = +std::numeric_limits<double>::infinity();
        max_v = -std::numeric_limits<double>::infinity();
    }
    double avg() const {
        return sum / n_v;
    }
    double min() const {
        return min_v;
    }
    double max() const {
        return max_v;
    }
    size_t n() const {
        return n_v;
    }
    StatCounter& operator+=(const StatCounter& other) {
        this->sum += other.sum;
        this->n_v += other.n_v;
        this->max_v = std::max(this->max_v, other.max_v);
        this->min_v = std::min(this->min_v, other.min_v);
        return *this;
    }
    friend StatCounter operator+(StatCounter a, const StatCounter& b) {
        return a += b;
    }
};
