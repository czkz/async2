#pragma once
#include <iomanip>
#include <string>
#include <string_view>
#include <map>
#include <vector>
#include <stdexcept>
#include <sstream>

namespace http {

    ///case-sensitive
    class view {
    public:
        std::string_view full;
        std::string_view status_line;
        std::string_view version;
        std::string_view version_number;
        std::string_view response_code;
        std::string_view response_message;
        std::string_view response_status;
        std::string_view request_method;
        std::string_view request_uri;
        std::string_view headers;
        std::string_view body;
        std::string_view before_body;  // request + \r\n + headers + \r\n\r\n
        constexpr view(std::string_view full) : full(full) {
            auto p1 = full.find("\r\n");
            auto p2 = full.find("\r\n\r\n");
            status_line = full.substr(0, p1);
            auto pr1 = status_line.find(' ');
            auto pr2 = status_line.find(' ', pr1 + 1);
            auto r1 = status_line.substr(0, pr1);
            auto r2 = status_line.substr(pr1 + 1, pr2 - (pr1 + 1));
            auto r3 = status_line.substr(pr2 + 1);
            auto r23 = status_line.substr(pr1 + 1);
            if (r1.size() > 4 && r1.substr(0, 4) == "HTTP") { // Response
                version = r1;
                response_status = r23;
                response_code = r2;
                response_message = r3;
            } else { // Request
                request_method = r1;
                request_uri = r2;
                version = r3;
            }
            version_number = version.substr(version.find('/') + 1);
            if (p2 != std::string_view::npos) {
                headers = full.substr(p1 + 2, p2 - p1);
                body = full.substr(p2 + 4);
                before_body = full.substr(0, p2 + 4);
            } else {
                throw std::runtime_error("http::view couldn't find \"\\r\\n\\r\\n\"");
                // headers = full.substr(p1 + 2);
                // body = full.substr(full.size());
                // before_body = full;
            }
        }
        bool has(std::string_view header, std::string_view value = "") const {
            auto line = find_line(header);
            return !line.empty() && line.find(value) != std::string::npos;
        }
        std::string_view get(std::string_view header) const {
            return find_line(header);
        }
        std::vector<std::string_view> getArray(std::string_view header) const {
            using std::string;
            const auto line = find_line(header);
            string::size_type pos1 = 0;
            string::size_type pos2;
            std::vector<std::string_view> v;
            while (pos1 < line.size()) {
                if (line[pos1] == ' ') { pos1++; }
                pos2 = line.find(",", pos1);
                if (pos2 == string::npos) { pos2 = line.size(); }
                v.emplace_back(line.data() + pos1, pos2 - pos1);
                pos1 = pos2 + 1;
            }
            return v;
        }

    private:
        static size_t ifind(std::string_view str, std::string_view substr) {
            size_t sz1 = str.size();
            size_t sz2 = substr.size();
            if (sz2 > sz1) { return -1; }
            for (size_t i = 0; i < sz1 - sz2 + 1; i++) {
                size_t j;
                for (j = 0; j < sz2; j++) {
                    // Good enough tolower for allowed HTTP chars
                    const char c1 = str[i+j] & 31;
                    const char c2 = substr[j] & 31;
                    if (c1 != c2) { break; }
                }
                if (j == sz2) { return i; }
            }
            return -1;
        }
        std::string_view find_line(std::string_view header) const {
            std::string a1 = "\r\n";
            a1 += header;
            a1 += ":";
            auto i1 = ifind(headers, a1);
            if (i1 == std::string::npos) {
                return std::string_view();
            }
            i1 += a1.size();
            auto i2 = headers.find("\r\n", i1);
            if (i2 == std::string::npos) {
                throw std::out_of_range("invalid http request");
            }
            return std::string_view(headers.data() + i1, i2 - i1);
        }
    };



    class packet {
    public:
        packet(std::string request) : request(std::move(request)) { }
        std::string request;
        std::string body;
        std::map<std::string, std::vector<std::string>, std::less<>> headers;
        void push(const std::string& header, std::string value) {
            headers[header].push_back(std::move(value));
        }
        void push(std::string_view full) {
            std::string_view header = full.substr(0, full.find(":"));
            std::string_view value = full.substr(full.find(":") + 1);
            if (value[0] == ' ') {
                value = full.substr(full.find(":") + 2);
            }
            headers[(std::string) header].push_back(std::string(value));
        }
        std::string str() {
            std::string s = request;
            s += "\r\n";
            for (const auto& e : headers) {
                s += e.first;
                s += ": ";
                for (const auto& v : e.second) {
                    s += v;
                    s += ", ";
                }
                s.resize(s.size() - 2);
                s += "\r\n";
            }
            s += "\r\n";
            s += body;
            return s;
        }
        void clear() {
            headers.clear();
        }
    };

    inline std::string encode_uri(std::string_view s) {
        std::ostringstream ss;
        ss << std::hex;
        for (const auto c : s) {
            bool keep =
                (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                (c == '-' || c == '_' || c == '.');
            if (keep) {
                ss << c;
            } else {
                ss << '%';
                ss << std::setw(2) << std::setfill('0');
                ss << +static_cast<uint8_t>(c);
            }
        }
        return ss.str();
    }

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


    // std::string recv_head(SockConnection& sock) {
    //     std::string s;
    //     do {
    //         s += sock.ReceiveAvailable();
    //     } while (s.find("\r\n\r\n") == std::string::npos);
    //     return s;
    // }
}
