#pragma once
#include "udp_raw.h"
#include "file.h"
#include <dns.h>
#include <unordered_map>

namespace async::dns::detail::conf_parsing {
    inline constexpr bool is_ws(char c) {
        return c == ' ' || c == '\t';
    }
    inline constexpr bool is_eol(char c) {
        return c == '#' || c == '\n';
    }
    inline constexpr bool is_w(char c) {
        return !is_ws(c) && !is_eol(c); 
    }
    inline void skip_ws(std::string_view s, size_t& i) {
        while (is_ws(s[i])) { i++; }
    }
    inline std::string_view skip_w(std::string_view s, size_t& i) {
        size_t i0 = i;
        while (is_w(s[i])) { i++; }
        return s.substr(i0, i - i0);
    }
    inline std::vector<std::string_view> skip_line(std::string_view line) {
        assert(line.ends_with('\n'));
        std::vector<std::string_view> ret;
        size_t i = 0;
        while (true) {
            skip_ws(line, i);
            if (is_eol(line[i])) { break; }
            ret.push_back(skip_w(line, i));
        }
        return ret;
    }
}

namespace async::dns::detail {
    inline task<std::string> parse_resolvconf() {
        stream stream = co_await file::open_read("/etc/resolv.conf");
        std::vector<std::string> ips;
        try {
            while (true) {
                std::string line = co_await stream.read_until("\n");
                if (line.empty() || line.back() != '\n') { line.push_back('\n'); }
                auto words = detail::conf_parsing::skip_line(line);
                if (words.size() == 2 && words[0] == "nameserver") {
                    ips.emplace_back(words[1]);
                    for (auto& c : ips.back()) { c = tolower(c); }
                }
            }
        } catch (const c_api::eof&) {}
        if (ips.empty()) {
            // This is what libc does
            ips.emplace_back("127.0.0.1");
        }
        co_return ips[0];
    }

    inline task<std::unordered_map<size_t, std::string>> parse_hosts() {
        stream stream = co_await file::open_read("/etc/hosts");
        std::unordered_map<size_t, std::string> host_to_ip;
        try {
            while (true) {
                std::string line = co_await stream.read_until("\n");
                if (line.empty() || line.back() != '\n') { line.push_back('\n'); }
                auto words = detail::conf_parsing::skip_line(line);
                if (words.size() > 1) {
                    for (auto& c : line) { c = tolower(c); }
                    try {
                        for (size_t i = 1; i < words.size(); i++) {
                            host_to_ip.emplace(std::hash<std::string_view>{}(words[i]), words[0]);
                        }
                    } catch (const std::exception&) {}
                }
                words.clear();
            }
        } catch (const c_api::eof&) {}
        co_return host_to_ip;
    }

    class cache_t {
    public:
        using cache_table_t = std::unordered_map<size_t, std::string>;
        task<std::string_view> get_server_ip() {
            if (dns_server_ip.empty()) {
                dns_server_ip = co_await detail::parse_resolvconf();
            }
            co_return dns_server_ip;
        }
        task<std::string> get_cache(size_t host_hash) {
            if (!has_etchosts) {
                cache_table.merge(co_await detail::parse_hosts());
                has_etchosts = true;
            }
            auto iter = cache_table.find(host_hash);
            if (iter != cache_table.end()) {
                co_return iter->second;
            }
            co_return std::string();
        }
        void put_cache(size_t host_hash, std::string ip_str) {
            cache_table.emplace(host_hash, ip_str);
        }
    private:
        std::string dns_server_ip;
        bool has_etchosts = false;
        cache_table_t cache_table;
    };

    // Used in host_to_ip() and ip_to_host()
    inline thread_local cache_t cache;
}

namespace async::dns {
    inline task<::dns::packet_t> dns_query(std::string_view ip, const ::dns::packet_t& req) {
        msgstream stream = co_await udp::detail::connect_udp_nolookup(ip, 53);
        co_await stream.write(req.str());
        while (true) {
            ::dns::packet_t resp = ::dns::packet_t::from_string(co_await stream.read());
            if (resp.flags.qr != 1) { continue; }
            if (resp.id != req.id) { continue; }
            co_return resp;
        }
    }

    inline task<std::string> lookup(std::string_view host, std::string_view dns_server_ip) {
        ::dns::packet_t resp = co_await dns_query(dns_server_ip, ::dns::standard_query(host));
        resp.throw_rcode();
        uint32_t ip = -1;
        for (const auto& ans : resp.answer_RRs) {
            if (ans.rname != host) { continue; }
            if (!::dns::is_A_RR(ans)) { continue; }
            ip = ::dns::from_A_RR(ans);
            break;
        }
        if (ip == -1u) { throw ex::runtime("no valid answers in DNS response"); }
        std::string ip_str = c_api::inet_htop(AF_INET, ip);
        co_return ip_str;
    }

    inline task<std::optional<std::string>> reverse_lookup(std::string_view ip, std::string_view dns_server_ip) {
        ::dns::packet_t req = ::dns::reverse_query(ip);
        ::dns::packet_t resp = co_await dns_query(dns_server_ip, req);
        if (resp.flags.rcode == ::dns::rcode_t::name_error) { // No such name
            co_return std::nullopt;
        }
        resp.throw_rcode();
        co_return std::nullopt;
        for (const auto& ans : resp.answer_RRs) {
            if (!::dns::is_PTR_RR(ans)) { continue; }
            if (ans.rname != req.questions[0].qname) { continue; }
            co_return ::dns::from_PTR_RR(ans);
        }
        throw ex::runtime("no valid answers in DNS response");
    }

    // Lookup with /etc/hosts and cache. Noop if host is already an ip address.
    inline task<std::string> host_to_ip(std::string_view host) {
        // Return as is if host is already an ip address
        try {
            (void) c_api::inet_pton(AF_INET, host);
            co_return std::string(host);
        } catch (std::exception&) {}

        // Lookup in cache
        const size_t host_hash = std::hash<std::string_view>{}(host);
        if (auto s = co_await detail::cache.get_cache(host_hash); !s.empty()) {
            co_return s;
        }

        // Make a DNS request
        auto dns_server_ip = co_await detail::cache.get_server_ip();
        std::string ip_str = co_await lookup(host, dns_server_ip);
        detail::cache.put_cache(host_hash, ip_str);
        co_return ip_str;
    }

    inline task<std::optional<std::string>> ip_to_host(std::string_view ip) {
        co_return co_await reverse_lookup(ip, co_await detail::cache.get_server_ip());
    }
}
