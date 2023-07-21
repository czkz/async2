#pragma once
#include <Stopwatch.h>
#include <string_view>
#include <ostream>
#include <iomanip>

class BenchLog {
    struct NullBuffer : std::streambuf { int overflow(int c) { return c; } };
    struct NullStream : std::ostream { NullStream() : std::ostream(&b) {} NullBuffer b; };
    inline static NullStream nopstream;
    std::ostream& o;
    Stopwatch st;
public:
    explicit BenchLog(std::ostream& o = nopstream) : o(o) {}
    BenchLog(const BenchLog&) = delete;
    void operator()(std::string_view str) {
        float t = st.tick();
        auto old_pres = o.precision();
        auto old_flags = o.flags();
        o << std::setprecision(1) << std::fixed
            << "[+" << std::setw(6) << t << "] " << str << '\n';
        o.flags(old_flags);
        o << std::setprecision(old_pres);
    }
    void reset() {
        st.tick();
    }
    BenchLog clone() {
        return BenchLog(o);
    }
};
