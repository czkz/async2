#pragma once
#include <iomanip>
#include <string>
#include <ctime>

inline std::string fmt_time() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::ostringstream ss;
    ss << '[' << std::put_time(&tm, "%T") << ']';
    return ss.str();
}
