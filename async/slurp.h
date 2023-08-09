#include "file.h"
#include "tcp.h"
#include "tls.h"
#include <http.h>

namespace async::detail {
    struct uri_view {
        std::optional<std::string_view> proto;
        std::optional<std::string_view> host;
        std::optional<uint16_t> port;
        std::optional<std::string_view> path;
    };

    inline auto parse_host_port(std::string_view host_port) {
        std::pair<std::string_view, std::optional<std::string_view>> ret;
        const auto i_colon = host_port.find(':');
        if (i_colon != host_port.npos) {
            ret.first = host_port.substr(0, i_colon);
            ret.second = host_port.substr(i_colon + 1);
        } else {
            ret.first = host_port;
        }
        return ret;
    }

    inline uri_view parse_uri(std::string_view uri) {
        const auto i_proto_end = uri.find("://");
        if (i_proto_end == uri.npos) {
            return {
                .proto = std::nullopt,
                .host = std::nullopt,
                .port = std::nullopt,
                .path = uri,
            };
        }
        const auto proto = uri.substr(0, i_proto_end);
        // handle path with no proto but :// in path
        for (auto c : proto) {
            if (!isalnum(c)) {
                return {
                    .proto = std::nullopt,
                    .host = std::nullopt,
                    .port = std::nullopt,
                    .path = uri,
                };
            }
        }
        const auto i_host_start = i_proto_end + 3;
        auto i_host_end = uri.find("/", i_host_start);
        if (i_host_end == uri.npos) {
            i_host_end = uri.size();
        }
        const auto host_port = uri.substr(i_host_start, i_host_end - i_host_start);
        const auto [host, opt_port] = parse_host_port(host_port);
        const auto path = uri.substr(i_host_end);
        std::optional<uint16_t> opt_port_num;
        if (opt_port) {
            if (opt_port->size() > 5) {
                throw ex::runtime("invalid uri port");
            }
            opt_port_num = std::stoi(std::string(*opt_port));
        }
        return {
            .proto = proto,
            .host = host,
            .port = opt_port_num,
            .path = !path.empty() ? std::optional{path} : std::nullopt,
        };
    }

    inline task<std::string> slurp_http_https(const uri_view& uri, size_t level);

    inline task<std::string> slurp_http_stream(auto stream, const uri_view& uri, size_t level) {
        // using HTTP/1.0 to avoid chunked encoding
        co_await stream.write(fmt_raw(
            "GET ", uri.path.value_or("/"), " HTTP/1.0\r\n",
            "Host: ", uri.host.value(), "\r\n",
            "\r\n"));
        auto buf = co_await stream.read_until_eof();
        http::view resp {buf};
        if (resp.response_code.size() != 3) {
            throw ex::runtime("server returned malformed error status");
        }
        if (resp.response_code == "200") {
            co_return std::string(resp.body);
        } else if (resp.response_code.starts_with("30")) {
            std::string_view new_url = resp.get("Location");
            co_return co_await slurp_http_https(parse_uri(new_url), level + 1);
        } else {
            std::string_view c = resp.response_code;
            bool print_code = isdigit(c[0]) && isdigit(c[1]) && isdigit(c[2]);
            if (print_code) {
                throw ex::runtime("server returned error status", std::stoi(std::string(c)));
            } else {
                throw ex::runtime("server returned malformed error status");
            }
        }
    }

    inline task<std::string> slurp_http_https(const uri_view& uri, size_t level) {
        if (level >= 16) {
            throw ex::runtime("http redirect recursion too deep");
        }
        if (uri.proto == "https") {
            stream stream = co_await tls::connect(uri.host.value(), uri.port.value_or(443));
            co_return co_await slurp_http_stream(std::move(stream), uri, level);
        } else {
            stream stream = co_await tcp::connect(uri.host.value(), uri.port.value_or(80));
            co_return co_await slurp_http_stream(std::move(stream), uri, level);
        }
    }
}

namespace async {
    inline task<std::string> slurp(std::string_view path) {
        detail::uri_view uri = detail::parse_uri(path);
        if (uri.proto == "http" || uri.proto == "https") {
            co_return co_await detail::slurp_http_https(uri, 0);
        } else if (uri.proto->empty() || uri.proto == "file") {
            stream stream = co_await file::open_read(uri.path.value());
            co_return co_await stream.read_until_eof();
        } else {
            throw ex::runtime("slurp protocol not supported");
        }
    }
}
