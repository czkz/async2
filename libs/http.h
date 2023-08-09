#pragma once
#include <stdexcept>
#include <vector>

namespace http {
    class view {
    public:
        std::string_view full;
        std::string_view status_line;      // "GET / HTTP/1.1" or "HTTP/1.1 404 Not Found"
        std::string_view version;          // HTTP/1.1
        std::string_view version_number;   // 1.1
        std::string_view response_code;    // 404
        std::string_view response_message; // Not Found
        std::string_view response_status;  // 404 Not Found
        std::string_view request_method;   // GET
        std::string_view request_uri;      // /
        std::string_view before_body;      // everything before \r\n\r\n (inclusive)
        std::string_view body;             // everything after \r\n\r\n
        std::vector<std::pair<std::string_view, std::string_view>> headers;

        constexpr explicit view(std::string_view full) : full(full) {
            auto str = full;
            auto consume_line = [&str] () {
                auto lf = no_npos(str.find('\n'));
                auto ret = str.substr(0, lf);
                str = str.substr(lf + 1);
                trim_cr(ret);
                return ret;
            };
            status_line = consume_line();
            trim_cr(status_line);
            auto sp1 = no_npos(status_line.find(' ', 0));
            auto sp2 = no_npos(status_line.find(' ', sp1 + 1));
            auto word1 = slice(status_line, 0, sp1);
            auto word2 = slice(status_line, sp1 + 1, sp2);
            auto word3 = slice(status_line, sp2 + 1);
            if (word1.starts_with("HTTP")) { // Response
                version = word1;
                response_code = word2;
                response_message = word3;
                response_status = slice(status_line, sp1 + 1);
            } else { // Request
                request_method = word1;
                request_uri = word2;
                version = word3;
            }
            version_number = slice(version, no_npos(version.find('/')) + 1);
            while (true) {
                auto l = consume_line();
                if (l.empty()) { break; }
                auto sep = no_npos(l.find(':'));
                auto k = slice(l, 0, sep);
                auto v = slice(l, sep + 1);
                while (v.starts_with(' ')) {
                    v = slice(v, 1);
                }
                headers.emplace_back(k, v);
            }
            before_body = slice(full, 0, full.size() - str.size());
            body = str;
        }
        constexpr bool has(std::string_view header, std::string_view value = "") const {
            const auto v = get(header);
            return !v.empty() && (value.empty() || v == value);
        }
        constexpr std::string_view get(std::string_view header) const {
            for (const auto& [k, v] : headers) {
                if (iequal(k, header)) {
                    return v;
                }
            }
            return std::string_view();
        }

    private:
        constexpr static bool iequal(std::string_view a, std::string_view b) {
            const size_t sz1 = a.size();
            const size_t sz2 = b.size();
            if (sz1 != sz2) { return false; }
            for (size_t i = 0; i < sz1; i++) {
                // Good enough tolower for allowed HTTP chars
                const char c1 = a[i] & 31;
                const char c2 = b[i] & 31;
                if (c1 != c2) { return false; }
            }
            return true;
        }
        constexpr static size_t no_npos(size_t i) {
            if (i == -1ull) {
                throw std::runtime_error("invalid http packet");
            }
            return i;
        }
        constexpr static void trim_cr(std::string_view& str) {
            if (str.ends_with('\r')) {
                str = str.substr(0, str.size() - 1);
            }
        }
        constexpr static std::string_view slice(std::string_view s, size_t i0, size_t i1) {
            return s.substr(i0, i1 - i0);
        }
        constexpr static std::string_view slice(std::string_view s, size_t i0) {
            return s.substr(i0);
        }
    };

    [[nodiscard]]
    constexpr inline std::string encode_uri(std::string_view s) {
        std::string ret;
        for (const auto c : s) {
            bool keep =
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                (c == '-' || c == '_' || c == '.');
            if (keep) {
                ret.push_back(c);
            } else {
                ret.push_back('%');
                uint8_t u8 = static_cast<uint8_t>(c);
                uint8_t c_high = u8 >> 4;
                uint8_t c_low = u8 & 0b00001111;
                ret.push_back(c_high + (c_high < 10 ? '0' : 'A'));
                ret.push_back(c_low + (c_low < 10 ? '0' : 'A'));
            }
        }
        return ret;
    }

    [[nodiscard]]
    inline std::string decode_uri(std::string_view s) {
        std::string ret;
        const size_t sz = s.size();
        std::string buf;
        for (size_t i = 0; i < sz; i++) {
            if (s[i] == '%') {
                if (i + 2 >= sz) { break; }
                buf = s.substr(i + 1, 2);
                ret += static_cast<char>(std::stoul(buf, nullptr, 16));
                i += 2;
            } else {
                ret += s[i];
            }
        }
        return ret;
    }
}
