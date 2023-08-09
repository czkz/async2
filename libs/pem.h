#pragma once
#include <base64.h>
#include <stdexcept>
#include <vector>

namespace pem {
    struct object {
        std::string label;
        std::string data;
    };
}

namespace pem::detail {
    inline void ensure(bool b) {
        if (!b) { throw std::runtime_error("malformed PEM"); }
    }
    [[nodiscard]]
    inline std::string_view consume_line(std::string_view& str) {
        ensure(!str.empty());
        size_t i = str.find('\n');
        if (i == str.npos) {
            auto ret = str;
            str = {};
            return ret;
        } else {
            auto ret = str.substr(0, i); // Without LF
            str = str.substr(i + 1);     // With LF
            if (ret.ends_with('\r')) {
                ret = ret.substr(0, ret.size() - 1);
            }
            return ret;
        }
    }
    [[nodiscard]]
    inline std::string_view try_header(bool begin, std::string_view l) {
        std::string_view beg = begin ? "-----BEGIN " : "-----END ";
        if (l.starts_with(beg)) {
            ensure(l.ends_with("-----"));
            auto label = l.substr(beg.size(), l.size() - beg.size() - 5);
            return label;
        }
        return {};
    }
}

namespace pem {
    inline std::vector<object> parse_all(std::string_view str) {
        using namespace detail;
        std::vector<object> ret;
        while (!str.empty()) {
            auto label1 = try_header(true, consume_line(str));
            if (label1.empty()) { continue; }
            std::string data;
            while (true) {
                auto l = consume_line(str);
                if (auto label2 = try_header(false, l); !label2.empty()) {
                    ensure(label1 == label2);
                    ret.push_back({
                        .label = std::string(label1),
                        .data = std::move(data),
                    });
                    break;
                }
                // l.size() should be 64, so this is fine
                from_base64(l, data);
            }
        }
        return ret;
    }
    inline std::vector<std::string> parse_certs(std::string_view str) {
        auto all = parse_all(str);
        std::vector<std::string> ret;
        for (auto& e : all) {
            if (
                e.label == "CERTIFICATE" ||
                e.label == "X509 CERTIFICATE" ||
                e.label == "X.509 CERTIFICATE"
            ) {
                ret.push_back(std::move(e.data));
            }
        }
        return ret;
    }
}
