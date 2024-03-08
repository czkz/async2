#pragma once
#include <string>
#include <string_view>
#include <ranges>

inline namespace string_conv {
    inline std::string to_string(char           value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%c"  , value)); return ret; }
    inline std::string to_string(short          value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%hd" , value)); return ret; }
    inline std::string to_string(int            value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%d"  , value)); return ret; }
    inline std::string to_string(long           value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%ld" , value)); return ret; }
    inline std::string to_string(long long      value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%lld", value)); return ret; }
    inline std::string to_string(unsigned char  value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%hhu", value)); return ret; }
    inline std::string to_string(unsigned short value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%hu" , value)); return ret; }
    inline std::string to_string(unsigned int   value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%u"  , value)); return ret; }
    inline std::string to_string(size_t         value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%zu" , value)); return ret; }
    inline std::string to_string(float          value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%.2f", value)); return ret; }
    inline std::string to_string(double         value) { std::string ret (64, 0); ret.resize(snprintf(ret.data(), ret.size(), "%.2f", value)); return ret; }
    inline std::string to_string(bool value) { return (value ? "true" : "false"); }
    inline std::string to_string(const char* value) { return value; }
    inline std::string to_string(const std::string& value) { return std::string(value); }
    inline std::string to_string(std::string_view value) { return std::string(value); }
    inline std::string to_string(std::ranges::range auto const& value) {
        std::string ret = "{ ";
        for (const auto& e : value) {
            ret += to_string(e);
            ret += ' ';
        }
        ret += '}';
        return ret;
    }
    template <typename T1, typename T2>
    inline std::string to_string(std::pair<T1, T2> const& value) {
        std::string ret = "{ ";
        ret += to_string(value.first);
        ret += ' ';
        ret += to_string(value.second);
        ret += " }";
        return ret;
    }
    template <typename T> inline std::string to_string(const T* value) {
        std::string ret (64, 0);
        ret.resize(snprintf(ret.data(), ret.size(), "%p", static_cast<const void*>(value)));
        return ret;
    }
}

template <typename T, typename... Ts>
std::string fmt_raw(const T& value, const Ts&... other) {
    using namespace string_conv;
    std::string ret = to_string(value);
    if constexpr (sizeof...(other) != 0) {
        ret += fmt_raw(other...);
    }
    return ret;
}

template <typename T, typename... Ts>
std::string fmt_sep(std::string_view sep, const T& value, const Ts&... other) {
    using namespace string_conv;
    std::string ret = to_string(value);
    if constexpr (sizeof...(other) != 0) {
        ret += sep;
        ret += fmt_sep(sep, other...);
    }
    return ret;
}

template <typename... Ts>
std::string fmt(const Ts&... args) {
    return fmt_sep(" ", args...);
}

template <typename... Ts>
void prn(const Ts&... args) {
    printf("%s\n", fmt(args...).data());
}

template <typename... Ts>
void prn_raw(const Ts&... args) {
    printf("%s\n", fmt_raw(args...).data());
}

template <typename... Ts>
std::string pad_start(std::string_view padding, const Ts&... args) {
    std::string s = fmt_raw(args...);
    if (s.size() >= padding.size()) {
        return s;
    } else {
        std::string ret{padding};
        size_t off = ret.size() - s.size();
        for (size_t i = 0; i < s.size(); i++) {
            ret[off+i] = s[i];
        }
        return ret;
    }
}

template <typename... Ts>
std::string pad_end(std::string_view padding, const Ts&... args) {
    std::string s = fmt_raw(args...);
    if (s.size() >= padding.size()) {
        return s;
    } else {
        std::string ret{padding};
        for (size_t i = 0; i < s.size(); i++) {
            ret[i] = s[i];
        }
        return ret;
    }
}
