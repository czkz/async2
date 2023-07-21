#pragma once
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <cassert>


#ifndef dp
#define dp(var) (_dp<false>((var), (#var)))
#define dpw(var) (_dp<true>((var), (#var)))
#define dv(var) (_dp<false>((void*)(var), (#var)))
#endif

template <typename T, typename = void>
struct is_iterable : std::false_type {};
template <typename T>
struct is_iterable<T, std::void_t<
    decltype(std::begin(std::declval<T>())),
    decltype(std::end(std::declval<T>()))
>> : std::true_type
{};

template <typename T, typename = void>
struct has_size : std::false_type {};
template <typename T>
struct has_size<T, std::void_t<
    decltype(std::size(std::declval<T>()))
>> : std::true_type
{};

template <typename T, typename = void>
struct is_printable : std::false_type {};
template <typename T>
struct is_printable<T, std::void_t<
    decltype(std::declval<std::ostream>() << std::declval<T>())
>> : std::true_type
{};

template<class T> struct dependent_false : std::false_type {};

template <bool W, typename T>
void _dp(const T& var, std::string_view name) {
    auto o = []() { if constexpr (W) { return std::wostringstream(); } else { return std::ostringstream(); } }();
    auto* oo = []() { if constexpr (W) { return &std::wcout; } else { return &std::cout; } }();
    if constexpr (std::is_array<T>::value) {
        using elemT = typename std::remove_reference<decltype(*var)>::type;
        if constexpr (std::is_same<elemT, const char>::value) {
            if (var == name.substr(1, name.length() - 2)) {
                o << var << std::endl;
                (*oo) << o.str();
                return;
            }
        }
    }
    if constexpr (!std::is_array<T>::value && is_printable<decltype(var)>::value) {
        o << name << ": " << var << std::endl;
    } else if constexpr (is_iterable<decltype(var)>::value) {
        if constexpr (has_size<decltype(var)>::value) {
            o << name << " = [" << std::size(var) << "] { ";
        } else {
            o << name << " = [?] { ";
        }
        if (std::begin(var) != std::end(var)) {
            for(const auto& e : var) {
                if constexpr (std::is_same<decltype(e), const char&>::value) {
                    o << +(unsigned char)e << ", ";
                } else {
                    o << e << ", ";
                }
            }
            o.seekp(-2, std::ios::end);
            o << " }" << std::endl;
        } else {
            o << '}' << std::endl;
        }
    } else {
        static_assert(dependent_false<T>::value, "Variable not printable");
    }
    (*oo) << o.str();
}

inline auto hd(std::string_view s) -> std::string {
    std::ostringstream ss;
    ss << std::hex;
    int l = 0;
    for (const auto c : s) {
        ss << std::setw(2) << std::setfill('0');
        ss << +static_cast<uint8_t>(c) << ' ';
        if (++l == 16) {
            ss << '\r' << std::endl;
            l = 0;
        }
    }
    if (l != 0) {
        ss << '\r' << std::endl;
    }
    return ss.str();
}

inline void dump(std::string_view s, const std::string& fname = "dump.txt") {
    std::ofstream(fname, std::ios::binary).write(s.data(), s.size());
}

inline void wdump(std::wstring_view s, const std::string& fname = "dump.txt") {
    std::wofstream(fname, std::ios::binary).write(s.data(), s.size());
}

inline std::string undump(const std::string& fname = "dump.txt") {
    std::ifstream f(fname, std::ios::binary);
    if (f.fail()) {
        throw std::runtime_error("undump() failed to open " + fname);
    }

    f.seekg (0, f.end);
    const auto len = f.tellg();
    f.seekg (0, f.beg);

    std::string s;
    s.resize(len);

    f.read(s.data(), len);
    f.close();
    return s;
}

inline std::wstring wundump(const std::string& fname = "dump.txt") {
    std::wifstream f(fname, std::ios::binary);

    f.seekg (0, f.end);
    const auto len = f.tellg();
    f.seekg (0, f.beg);

    std::wstring s;
    s.resize(len);

    f.read(s.data(), len);
    f.close();
    return s;
}

template <typename Func>
class defer {
    Func fn;
public:
    explicit constexpr defer(Func fn) noexcept : fn(fn) {}
    ~defer() noexcept { fn(); }
};
