#include "asn1.hpp"

// Added for ASN.1 sources (plan 21 step LIB84): BER TLVs as asn1rt_nif decodes and encodes them, with its error
// positions and limits (tags up to two bytes after the first, lengths of at most four bytes).
namespace clause::runtime::builtins::asn1 {
namespace {
constexpr std::uint8_t CLASS_MASK = 0xC0;
constexpr std::uint8_t FORM_MASK = 0x20;
constexpr std::uint8_t NUMBER_MASK = 0x1F;
// Nesting deeper than this fails as the NIF's stack guard does.
constexpr std::size_t DEPTH_LIMIT = 1000;

[[noreturn]] void fail(const std::string_view reason, const std::size_t position) { throw TlvError{reason, position}; }

// The parsing state of one decode_ber_tlv call.
class TlvParser {
  public:
    explicit TlvParser(std::span<const std::byte> input) : input_(input) {}

    Tlv tlv(std::size_t end, std::size_t depth);

    std::size_t position() const { return at_; }

  private:
    std::uint8_t byte(const std::size_t index) const { return static_cast<std::uint8_t>(input_[index]); }

    void tag(Tlv &tlv, std::size_t end);
    void value(Tlv &tlv, std::size_t end, std::size_t depth);
    void indefinite(Tlv &tlv, std::size_t end, std::size_t depth);
    std::size_t definite_length(std::size_t end);

    std::span<const std::byte> input_;
    std::size_t at_ = 0;
};

Tlv TlvParser::tlv(const std::size_t end, const std::size_t depth) {
    if (at_ + 2 > end) {
        fail("invalid_value", at_);
    }
    Tlv result;
    tag(result, end);
    if (at_ >= end) {
        fail("invalid_tag", at_);
    }
    value(result, end, depth);
    return result;
}

void TlvParser::tag(Tlv &tlv, const std::size_t end) {
    const auto first = byte(at_);
    tlv.tag_ = static_cast<std::uint64_t>(first & CLASS_MASK) << 10U;
    tlv.constructed_ = (first & FORM_MASK) != 0;
    if ((first & NUMBER_MASK) < 31) {
        tlv.tag_ |= first & NUMBER_MASK;
        ++at_;
        return;
    }
    if (at_ + 3 > end) {
        fail("invalid_value", at_);
    }
    ++at_;
    if (byte(at_) >= 128) {
        tlv.tag_ |= static_cast<std::uint64_t>(byte(at_) & 0x7FU) << 7U;
        ++at_;
    }
    if (byte(at_) >= 128) {
        fail("invalid_tag", at_);
    }
    tlv.tag_ |= byte(at_);
    ++at_;
}

void TlvParser::value(Tlv &tlv, const std::size_t end, const std::size_t depth) {
    if (depth >= DEPTH_LIMIT) {
        fail("unknown", at_);
    }
    if (byte(at_) == 0x80) {
        indefinite(tlv, end, depth);
        return;
    }
    const auto length = (byte(at_) & 0x80U) == 0 ? byte(at_) : definite_length(end);
    if (length > end - (at_ + 1)) {
        fail("invalid_value", at_);
    }
    ++at_;
    if (!tlv.constructed_) {
        tlv.value_ = input_.subspan(at_, length);
        at_ += length;
        return;
    }
    const auto inner_end = at_ + length;
    while (at_ < inner_end) {
        tlv.children_.push_back(this->tlv(inner_end, depth + 1));
    }
}

// Indefinite length: TLVs up to the two zero bytes; primitive forms may not use it.
void TlvParser::indefinite(Tlv &tlv, const std::size_t end, const std::size_t depth) {
    ++at_;
    if (at_ + 1 >= end || !tlv.constructed_) {
        fail("invalid_length", at_);
    }
    while (byte(at_) != 0 || byte(at_ + 1) != 0) {
        tlv.children_.push_back(this->tlv(end, depth + 1));
        if (at_ + 1 >= end) {
            fail("invalid_length", at_);
        }
    }
    at_ += 2;
}

// The long form: a count of length bytes, the length in at most four of them; leaves the cursor on the last one.
std::size_t TlvParser::definite_length(const std::size_t end) {
    auto count = byte(at_) & 0x7FU;
    if (count > end - (at_ + 1)) {
        fail("invalid_length", at_);
    }
    std::size_t length = 0;
    while (count-- != 0) {
        ++at_;
        if (length >= (std::size_t{1} << 24U)) {
            fail("invalid_length", at_);
        }
        length = (length << 8U) + byte(at_);
    }
    return length;
}

void append_length(std::vector<std::byte> &out, std::size_t length) {
    if (length < 128) {
        out.push_back(static_cast<std::byte>(length));
        return;
    }
    std::vector<std::byte> digits;
    for (; length > 0; length >>= 8U) {
        digits.insert(digits.begin(), static_cast<std::byte>(length & 0xFFU));
    }
    out.push_back(static_cast<std::byte>(0x80U | digits.size()));
    out.insert(out.end(), digits.begin(), digits.end());
}

void append_tag(std::vector<std::byte> &out, const std::uint64_t tag, const bool constructed) {
    const auto head = static_cast<std::uint8_t>((constructed ? FORM_MASK : 0) | ((tag & 0x30000U) >> 10U));
    auto number = tag & 0xFFFFU;
    if (number <= 30) {
        out.push_back(static_cast<std::byte>(head | number));
        return;
    }
    std::vector<std::byte> digits{static_cast<std::byte>(number & 127U)};
    for (number >>= 7U; number > 0; number >>= 7U) {
        digits.insert(digits.begin(), static_cast<std::byte>((number & 127U) | 0x80U));
    }
    out.push_back(static_cast<std::byte>(head | NUMBER_MASK));
    out.insert(out.end(), digits.begin(), digits.end());
}
} // namespace

std::pair<Tlv, std::size_t> parse_tlv(const std::span<const std::byte> input) {
    TlvParser parser(input);
    auto tlv = parser.tlv(input.size(), 0);
    return {std::move(tlv), parser.position()};
}

Term tlv_term(Builder &builder, const Tlv &tlv) {
    return builder.tuple({builder.integer(static_cast<std::int64_t>(tlv.tag_)), Data::content(tlv).term(builder)});
}

void append_tlv(std::vector<std::byte> &out, const Tlv &tlv) {
    append_tag(out, tlv.tag_, tlv.constructed_);
    if (!tlv.constructed_) {
        append_length(out, tlv.value_.size());
        out.insert(out.end(), tlv.value_.begin(), tlv.value_.end());
        return;
    }
    std::vector<std::byte> content;
    for (const auto &child : tlv.children_) {
        append_tlv(content, child);
    }
    append_length(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
}
} // namespace clause::runtime::builtins::asn1
