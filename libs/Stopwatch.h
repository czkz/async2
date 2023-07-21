#pragma once
#include <chrono>

class Stopwatch {
    typedef std::chrono::steady_clock clock;
    std::chrono::time_point<clock> t1 = clock::now();
    auto _getset() {
        auto t2 = clock::now();
        auto ret = t2 - t1;
        t1 = t2;
        return ret;
    }
    auto _get() const { return clock::now() - t1; }
public:
    ///@return Time in milliseconds since last tick
    double tick() { return (double) std::chrono::duration_cast<std::chrono::nanoseconds>(_getset()).count() / 10e5; }
    ///@return Time in milliseconds since last tick
    double ping() const { return (double) std::chrono::duration_cast<std::chrono::nanoseconds>(_get()).count() / 10e5; }
};
