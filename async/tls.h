#pragma once
#include "stream.h"
#include <bearssl.h>
#include "tcp.h"
#include <filesystem>
#include <pem.h>

namespace async::transport {
    template <typename Transport>
    class tls_client {
    public:
        explicit tls_client(std::string_view host, std::span<const br_x509_trust_anchor> ta_list, Transport transport) : transport(std::move(transport)) {
            iobuf.resize(BR_SSL_BUFSIZE_BIDI);
            xc = std::make_unique<br_x509_minimal_context>();
            cc = std::make_unique<br_ssl_client_context>();
            br_ssl_client_init_full(cc.get(), xc.get(), ta_list.data(), ta_list.size());
            br_ssl_engine_set_versions(&cc->eng, BR_TLS12, BR_TLS12);
            br_ssl_engine_set_buffer(&cc->eng, iobuf.data(), iobuf.size(), 1);
            br_ssl_client_reset(cc.get(), std::string(host).c_str(), 0);
        }
        tls_client(const tls_client&) = delete;
        tls_client(tls_client&&) = default;

        task<void> wait_read() {
            while (true) {
                auto st = get_state();
                if (st & BR_SSL_RECVAPP) {
                    co_return;
                } else if (st & BR_SSL_RECVREC) {
                    co_await transport.wait_read();
                    read_records();
                } else if (st & BR_SSL_SENDREC) {
                    co_await write_all_records();
                } else {
                    assert(false);
                }
            }
        }

        task<void> wait_write() {
            while (true) {
                auto st = get_state();
                if (st & BR_SSL_SENDAPP) {
                    co_return;
                } else if (st & BR_SSL_SENDREC) {
                    co_await write_all_records();
                } else if (st & BR_SSL_RECVREC) {
                    co_await transport.wait_read();
                    read_records();
                } else {
                    assert(false);
                }
            }
        }

        size_t read(void* buf, size_t size) {
            read_records();
            return read_app(buf, size);
        }

        size_t write(std::string_view data) {
            write_records();
            return write_app(data);
        }

        task<void> flush() {
            br_ssl_engine_flush(&cc->eng, 0);
            while (true) {
                auto st = get_state(false);
                if ((st & BR_SSL_SENDAPP) || (st & BR_SSL_CLOSED)) {
                    co_return;
                } else if (st & BR_SSL_SENDREC) {
                    co_await write_all_records();
                } else if (st & BR_SSL_RECVREC) {
                    co_await transport.wait_read();
                    read_records();
                } else {
                    assert(false);
                }
            }
        }

        task<void> close() {
            br_ssl_engine_flush(&cc->eng, 0);
            br_ssl_engine_close(&cc->eng);
            while (true) {
                auto st = get_state(false);
                if (st & BR_SSL_CLOSED) {
                    co_return;
                } else if (st & BR_SSL_SENDREC) {
                    co_await write_all_records();
                } else if (st & BR_SSL_RECVREC) {
                    co_await transport.wait_read();
                    read_records();
                } else {
                    assert(false);
                }
            }
        }

        static constexpr bool has_lookahead = true;
        size_t available_bytes() {
            if(!(get_state() & BR_SSL_SENDAPP)) {
                return 0;
            }
            size_t len;
            br_ssl_engine_sendapp_buf(&cc->eng, &len);
            return len;
        }

    private:
        uint16_t get_state(bool throw_ok = true) {
            auto st = br_ssl_engine_current_state(&cc->eng);
            if (st == BR_SSL_CLOSED) {
                int err = br_ssl_engine_last_error(&cc->eng);
                if (err == BR_ERR_OK) {
                    if (throw_ok) {
                        throw c_api::eof();
                    } else {
                        return st;
                    }
                } else {
                    if (err == BR_ERR_X509_NOT_TRUSTED) {
                        throw ex::runtime("server certificate not trusted");
                    } else if (err == BR_ERR_X509_EXPIRED) {
                        throw ex::runtime("server certificate expired");
                    } else if (err == BR_ERR_UNSUPPORTED_VERSION) {
                        throw ex::runtime("server tls version unsupported");
                    }
                    throw ex::runtime("bearssl error", err);
                }
            }
            return st;
        }

        void write_records() {
            if (!(get_state() & BR_SSL_SENDREC)) {
                return;
            }
            size_t len;
            uint8_t* buf = br_ssl_engine_sendrec_buf(&cc->eng, &len);
            size_t wlen = transport.write({reinterpret_cast<const char*>(buf), len});
            if (wlen > 0) {
                br_ssl_engine_sendrec_ack(&cc->eng, wlen);
            }
        }

        void read_records() {
            if (!(get_state() & BR_SSL_RECVREC)) {
                return;
            }
            size_t len;
            uint8_t* buf = br_ssl_engine_recvrec_buf(&cc->eng, &len);
            size_t rlen = transport.read(buf, len);
            if (rlen > 0) {
                br_ssl_engine_recvrec_ack(&cc->eng, rlen);
            }
        }

        size_t write_app(std::string_view data) {
            if (data.size() == 0) { return 0; }
            if(!(get_state() & BR_SSL_SENDAPP)) {
                return 0;
            }
            size_t len;
            uint8_t* buf = br_ssl_engine_sendapp_buf(&cc->eng, &len);
            size_t rlen = std::min(len, data.size());
            memcpy(buf, data.data(), rlen);
            br_ssl_engine_sendapp_ack(&cc->eng, rlen);
            return rlen;
        }

        size_t read_app(void* dest_buf, size_t dest_size) {
            if (dest_size == 0) { return 0; }
            if (!(get_state() & BR_SSL_RECVAPP)) {
                return 0;
            }
            size_t len;
            uint8_t* src_buf = br_ssl_engine_recvapp_buf(&cc->eng, &len);
            size_t rlen = std::min(dest_size, len);
            memcpy(dest_buf, src_buf, rlen);
            br_ssl_engine_recvapp_ack(&cc->eng, rlen);
            return rlen;
        }

        task<void> write_all_records() {
            bool should_flush = false;
            while (get_state() & BR_SSL_SENDREC) {
                co_await transport.wait_write();
                write_records();
                should_flush = true;
            }
            if (should_flush) {
                co_await transport.flush();
            }
        }

    private:
        // All these must be heap-allocated for move ctors to work,
        // because bearssl expects invariant addresses.
        std::unique_ptr<br_ssl_client_context> cc;
        std::unique_ptr<br_x509_minimal_context> xc;
        std::vector<uint8_t> iobuf;

    public:
        Transport transport;
    };
}

namespace async::tls::detail {
    struct ta_extra_data {
        std::unique_ptr<br_x509_decoder_context> dc;
        std::basic_string<uint8_t> dn;
    };

    struct ta_list {
        std::vector<ta_extra_data> eds;
        std::vector<br_x509_trust_anchor> tas;
    };

    inline void cert2ta(std::string_view raw_cert, ta_list& out) {
        static constexpr auto callback = [] (void *ctx, const void *buf, size_t len) {
            using bstr = std::basic_string<uint8_t>;
            bstr& dn = *reinterpret_cast<bstr*>(ctx);
            dn.append(reinterpret_cast<const bstr::value_type*>(buf), len);
        };
        ta_extra_data ed;
        ed.dc = std::make_unique<br_x509_decoder_context>();
	    br_x509_decoder_init(ed.dc.get(), callback, &ed.dn);
	    br_x509_decoder_push(ed.dc.get(), raw_cert.data(), raw_cert.size());
	    br_x509_pkey* pk = br_x509_decoder_get_pkey(ed.dc.get());
	    if (pk == nullptr) {
		    throw ex::runtime("certificate decoding failed", br_x509_decoder_last_error(ed.dc.get()));
	    }
	    br_x509_trust_anchor ta = {
            .dn = {
                .data = reinterpret_cast<uint8_t*>(ed.dn.data()),
                .len = ed.dn.size(),
            },
            .flags = br_x509_decoder_isCA(ed.dc.get()) ? BR_X509_TA_CA : 0u,
            .pkey = *pk,
        };
        out.eds.push_back(std::move(ed));
        out.tas.push_back(std::move(ta));
    }

    [[nodiscard]]
    inline ta_list pem_to_ta_list(std::string_view pem_data) {
        std::vector<std::string> certs = pem::parse_certs(pem_data);
        ta_list ret;
        ret.eds.reserve(certs.size());
        ret.tas.reserve(certs.size());
        for (const auto& cert : certs) {
            cert2ta(cert, ret);
        }
        return ret;
    }

    inline std::string_view default_certs_path() {
        using std::filesystem::exists;
        constexpr auto paths = std::to_array<std::string_view>({
            "/etc/ssl/cert.pem",
            "/etc/ssl/certs.pem",
        });
        for (const auto& path : paths) {
            if (exists(path)) { return path; }
        }
        throw ex::runtime("could not find default certificates");
    }

    inline task<std::string> get_default_certs_nocache() {
        stream stream = co_await file::open_read(detail::default_certs_path());
        std::string ret = co_await stream.read_until_eof();
        if (!ret.empty() && ret.back() != '\n') {
            ret.push_back('\n');
        }
        co_return ret;
    }
}

namespace async::tls {
    inline task<std::string_view> get_default_certs() {
        static const std::string certs = co_await detail::get_default_certs_nocache();
        co_return certs;
    }
}

namespace async::tls::detail {
    inline task<const ta_list*> get_default_ta_list() {
        // TODO UB when pem_to_ta_list throws
        static const ta_list l = detail::pem_to_ta_list(co_await get_default_certs());
        co_return &l;
    }
}

namespace async::tls {
    // certs can contain multiple certificates in PEM format.
    // Certificates start with "-----BEGIN CERTIFICATE-----",
    // end with "-----END CERTIFICATE-----",
    // and can be separated with any amount of lines (with or without text).
    // Default certificates end with a newline.
    inline task<transport::tls_client<transport::tcp_socket>> connect(
        std::string_view host,
        uint16_t port,
        std::optional<std::string_view> certs = std::nullopt
    ) {
        const detail::ta_list* list_ptr;
        std::optional<detail::ta_list> certs_list;
        if (certs) {
            certs_list = detail::pem_to_ta_list(*certs);
            list_ptr = &certs_list.value();
        } else {
            list_ptr = co_await detail::get_default_ta_list();
        }
        transport::tls_client ret {host, list_ptr->tas, co_await tcp::connect(host, port)};
        co_await ret.wait_write(); // Complete the handshake
        co_return ret;
    }
}
