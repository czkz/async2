#include <random>
#include <stdexcept>

// Structs
namespace dns {
    inline uint16_t random_id() {
        using engine_t = std::independent_bits_engine<std::default_random_engine, 16, uint16_t>;
        thread_local engine_t engine(std::random_device{}());
        return engine();
    }

    struct question_t {
        std::string qname;
        uint16_t qtype;
        uint16_t qclass;
    };

    struct resource_record_t {
        std::string rname;
        uint16_t rtype;
        uint16_t rclass;
        uint32_t ttl;
        std::string rdata;
    };

    enum class opcode_t : uint8_t {
        query = 0,
        iquery = 1,
        status = 2,
    };

    enum class rcode_t : uint8_t {
        no_error = 0,
        format_error = 1,
        server_failure = 2,
        name_error = 3,
        not_implemented = 4,
        refused = 5,
    };

    struct flags_t {
        uint8_t qr:      1;
        opcode_t opcode: 4;
        uint8_t aa:      1;
        uint8_t tc:      1;
        uint8_t rd:      1;
        uint8_t ra:      1;
        uint8_t z:       3;
        rcode_t rcode:   4;
    };

    inline constexpr std::string_view rcode_to_string(rcode_t rcode) noexcept {
        switch(static_cast<uint8_t>(rcode)) {
            case 0: return "No error";
            case 1: return "Format error";
            case 2: return "Server failure";
            case 3: return "Name error";
            case 4: return "Not implemented";
            case 5: return "Refused";
            default: return "Reserved";
        }
    }

    struct packet_t {
        uint16_t id;
        flags_t flags;
        std::vector<question_t> questions;
        std::vector<resource_record_t> answer_RRs;
        std::vector<resource_record_t> authority_RRs;
        std::vector<resource_record_t> additional_RRs;
        std::string str() const;
        static packet_t from_string(std::string_view);
        void throw_rcode() const;
    };
}

namespace dns::detail {
    class serializer {
    public:
        void push_byte(uint8_t v) {
            buf.push_back(v);
        }
        void push_string(std::string_view v) {
            buf.append(v);
        }
        void push_word(uint16_t v) {
            push_byte(v >> 8);
            push_byte(v >> 0);
        }
        void push_dword(uint32_t v) {
            push_byte(v >> 24);
            push_byte(v >> 16);
            push_byte(v >> 8);
            push_byte(v >> 0);
        }
        void push_label(std::string_view v) {
            // https://www.rfc-editor.org/rfc/rfc1035#section-2.3.4
            if (v.size() > 63) { throw std::runtime_error("bad domain name"); }
            push_byte(v.size());
            push_string(v);
        }
        void push_name(std::string_view v) {
            size_t i0 = 0;
            while (i0 < v.size()) {
                size_t i1 = v.find('.', i0);
                if (i1 == std::string_view::npos) { i1 = v.size(); }
                push_label(v.substr(i0, i1 - i0));
                i0 = i1 + 1;
            }
            push_byte('\0');
        };
        void push_question(const question_t& v) {
            push_name(v.qname);
            push_word(v.qtype);
            push_word(v.qclass);
        }
        void push_resource_record(const resource_record_t& v) {
            push_name(v.rname);
            push_word(v.rtype);
            push_word(v.rclass);
            push_dword(v.ttl);
            push_word(v.rdata.size());
            push_string(v.rdata);
        }
        void push_flags(const flags_t& v) {
            uint16_t w = 0;
            w |= (v.qr & 1u) << 15;
            w |= (static_cast<uint8_t>(v.opcode) & 0b1111u) << 11;
            w |= (v.aa & 1u) << 10;
            w |= (v.tc & 1u) << 9;
            w |= (v.rd & 1u) << 8;
            w |= (v.ra & 1u) << 7;
            w |= (v.z & 0b111u) << 4;
            w |= (static_cast<uint8_t>(v.rcode) & 0b1111u) << 0;
            push_word(w);
        }
        void push_packet(const packet_t& packet) {
            push_word(packet.id);
            push_flags(packet.flags);
            push_word(packet.questions.size());
            push_word(packet.answer_RRs.size());
            push_word(packet.authority_RRs.size());
            push_word(packet.additional_RRs.size());
            for (const auto& e : packet.questions) {
                push_question(e);
            }
            for (const auto& e : packet.answer_RRs) {
                push_resource_record(e);
            }
            for (const auto& e : packet.authority_RRs) {
                push_resource_record(e);
            }
            for (const auto& e : packet.additional_RRs) {
                push_resource_record(e);
            }
        }
    public:
        std::string buf;
    };

    class parser {
    public:
        explicit parser(std::string_view buf) : str(buf), full(str) {}
        size_t current_index() const {
            return str.data() - full.data();
        }
    private:
        void checklen(size_t len) const {
            if (len > str.size()) {
                throw std::runtime_error("unexpected DNS response end");
            }
        }
        void advance(size_t len) {
            str = str.substr(len);
        }
    public:
        [[nodiscard]]
        uint8_t byte() {
            checklen(1);
            uint8_t ret = str[0];
            advance(1);
            return ret;
        }
        [[nodiscard]]
        uint16_t word() {
            uint8_t b0 = byte();
            uint8_t b1 = byte();
            return (b0 << 8) | (b1 << 0);
        }
        [[nodiscard]]
        uint32_t dword() {
            uint8_t b0 = byte();
            uint8_t b1 = byte();
            uint8_t b2 = byte();
            uint8_t b3 = byte();
            return
                (b0 << 24) |
                (b1 << 16) |
                (b2 << 8) |
                (b3 << 0);
        }
        [[nodiscard]]
        std::string_view string(size_t len) {
            checklen(len);
            auto ret = str.substr(0, len);
            advance(len);
            return ret;
        }
        [[nodiscard]]
        std::string_view label() {
            uint16_t len = byte() & 0b00111111u;
            return string(len);
        }
        [[nodiscard]]
        std::string name(size_t level = 0) {
            if (level > 16) {
                throw std::runtime_error("recursion too deep in DNS response domain name");
            }
            std::string ret;
            for (size_t iterations = 0; iterations < 32; iterations++) {
                checklen(1);
                if (str[0] == '\0') {
                    (void) byte();
                    if (!ret.empty() && ret.back() == '.') { ret.pop_back(); }
                    return ret;
                } else if ((str[0] & 0b11000000u) == 0b11000000u) {
                    uint16_t offset = word() & 0b00111111'11111111u;
                    if (offset >= full.size()) {
                        throw std::runtime_error("invalid pointer in DNS response domain name");
                    }
                    ret.append(parser(full.substr(offset)).name(level + 1));
                    return ret;
                } else {
                    ret.append(label());
                    ret.push_back('.');
                }
            }
            throw std::runtime_error("record name too long in DNS response");
        }
        [[nodiscard]]
        question_t question() {
            return {
                .qname = name(),
                .qtype = word(),
                .qclass = word(),
            };
        }
        [[nodiscard]]
        resource_record_t resource_record() {
            return {
                .rname = name(),
                .rtype = word(),
                .rclass = word(),
                .ttl = dword(),
                .rdata = std::string(string(word())),
            };
        }
        flags_t flags() {
            uint16_t w = word();
            flags_t f = {
                .qr     = static_cast<uint8_t> ((w >> 15) & 1u),
                .opcode = static_cast<opcode_t>((w >> 11) & 0b1111u),
                .aa     = static_cast<uint8_t> ((w >> 10) & 1u),
                .tc     = static_cast<uint8_t> ((w >> 9) & 1u),
                .rd     = static_cast<uint8_t> ((w >> 8) & 1u),
                .ra     = static_cast<uint8_t> ((w >> 7) & 1u),
                .z      = static_cast<uint8_t> ((w >> 4) & 0b111u),
                .rcode  = static_cast<rcode_t> ((w >> 0) & 0b1111u),
            };
            return f;
        }
        [[nodiscard]]
        packet_t packet() {
            packet_t ret;
            ret.id    = word();
            ret.flags = flags();
            auto n_questions      = word();
            auto n_answer_RRs     = word();
            auto n_authority_RRs  = word();
            auto n_additional_RRs = word();
            for (size_t i = 0; i < n_questions; i++) {
                ret.questions.push_back(question());
            }
            for (size_t i = 0; i < n_answer_RRs; i++) {
                ret.answer_RRs.push_back(resource_record());
            }
            for (size_t i = 0; i < n_authority_RRs; i++) {
                ret.authority_RRs.push_back(resource_record());
            }
            for (size_t i = 0; i < n_additional_RRs; i++) {
                ret.additional_RRs.push_back(resource_record());
            }
            return ret;
        }
    private:
        std::string_view str;
        std::string_view full;
    };
}

// Out of line definitions
namespace dns {
    inline std::string packet_t::str() const {
        detail::serializer s;
        s.push_packet(*this);
        return std::move(s.buf);
    }

    inline packet_t packet_t::from_string(std::string_view s) {
        return detail::parser(s).packet();
    }

    inline void packet_t::throw_rcode() const {
        if (this->flags.rcode != rcode_t::no_error) {
            throw std::runtime_error("DNS server error: " + std::string(rcode_to_string(this->flags.rcode)));
        }
    }
}

// RR rdata converters
namespace dns {
    inline bool is_A_RR(const resource_record_t& rr) {
        return rr.rtype == 1 && rr.rclass == 1;
    }

    // Returns ip in host byte order
    inline uint32_t from_A_RR(const resource_record_t& rr) {
        return detail::parser(rr.rdata).dword();
    }

    inline std::string to_A_RR(uint32_t ip) {
        detail::serializer s;
        s.push_dword(ip);
        return std::move(s.buf);
    }

    inline bool is_PTR_RR(const resource_record_t& rr) {
        return rr.rtype == 12 && rr.rclass == 1;
    }

    inline std::string from_PTR_RR(const resource_record_t& rr) {
        return detail::parser(rr.rdata).name();
    }

    inline std::string to_PTR_RR(std::string_view host) {
        detail::serializer s;
        s.push_name(host);
        return std::move(s.buf);
    }
}

namespace dns::detail {
    inline std::string ip_to_inaddr_arpa_host(std::string_view ip) {
        std::string host;
        size_t i1 = ip.size();
        while (i1 != 0) {
            i1 -= 1;
            size_t i0 = ip.rfind('.', i1);
            if (i0 == std::string_view::npos) {
                host.append(ip.substr(0, i1 + 1));
                host.push_back('.');
                break;
            } else {
                host.append(ip.substr(i0 + 1, i1 - i0));
                host.push_back('.');
                i1 = i0;
            }
        }
        host.append("in-addr.arpa");
        return host;
    }
}

// Common queries
namespace dns {
    inline packet_t standard_query(question_t question) {
        return {
            .id = random_id(),
            .flags = flags_t {
                .qr = 0,
                .opcode = opcode_t::query,
                .aa = 0,
                .tc = 0,
                .rd = 1,
                .ra = 0,
                .z = 0,
                .rcode = rcode_t::no_error,
            },
            .questions = {
                std::move(question)
            },
            .answer_RRs = {},
            .authority_RRs = {},
            .additional_RRs = {},
        };
    }

    inline packet_t standard_query(std::string_view host) {
        return standard_query(question_t {
            .qname = std::string(host),
            .qtype = 1,  // Type: A
            .qclass = 1,  // Class: IN
        });
    }

    inline packet_t reverse_query(std::string_view ip) {
        return standard_query(question_t {
            .qname = detail::ip_to_inaddr_arpa_host(ip),
            .qtype = 12,  // Type: PTR
            .qclass = 1,  // Class: IN
        });
    }

    [[deprecated("These seem to be unsupported nowadays. Use reverse_query() instead.")]]
    inline packet_t inverse_query(uint32_t ip) {
        return {
            .id = random_id(),
            .flags = flags_t {
                .qr = 0,
                .opcode = opcode_t::iquery,
                .aa = 0,
                .tc = 0,
                .rd = 1,
                .ra = 0,
                .z = 0,
                .rcode = rcode_t::no_error,
            },
            .questions = {},
            .answer_RRs = {
                resource_record_t {
                    .rname = "",
                    .rtype = 1,   // Type: A
                    .rclass = 1,  // Class: IN
                    .ttl = 0,
                    .rdata = to_A_RR(ip),
                }
            },
            .authority_RRs = {},
            .additional_RRs = {},
        };
    }
}
