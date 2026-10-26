#include "../encoding/base64.hpp"
#include "public_key.hpp"
#include <array>
#include <string>

// Added for public_key's PEM text (plan 21 step LIB80E1): pubkey_pem's reading and writing of PEM text with OTP's
// labels, BEGIN lines with trailing white space, the legacy Proc-Type and DEK-Info header and base64 bodies cut into
// lines of 64 characters. The ENCRYPTED PRIVATE KEY entries stay for Erlang, which decodes them with der_decode/2.
namespace clause::runtime::builtins::public_key {
using asn1::erlang_error;
using Reason = abi::v1::ErrorReason;

namespace {
using Text = std::string_view;

// A PEM label and the type of its entries; the empty type is OpenSSH's {no_asn1, new_openssh}.
struct Label {
    Text type_;
    Text label_;
};

constexpr std::array<Label, 14> LABELS{{
    {"Certificate", "CERTIFICATE"},
    {"RSAPrivateKey", "RSA PRIVATE KEY"},
    {"RSAPublicKey", "RSA PUBLIC KEY"},
    {"SubjectPublicKeyInfo", "PUBLIC KEY"},
    {"DSAPrivateKey", "DSA PRIVATE KEY"},
    {"DHParameter", "DH PARAMETERS"},
    {"PrivateKeyInfo", "PRIVATE KEY"},
    {"EncryptedPrivateKeyInfo", "ENCRYPTED PRIVATE KEY"},
    {"CertificationRequest", "CERTIFICATE REQUEST"},
    {"ContentInfo", "PKCS7"},
    {"CertificateList", "X509 CRL"},
    {"EcpkParameters", "EC PARAMETERS"},
    {"ECPrivateKey", "EC PRIVATE KEY"},
    {"", "OPENSSH PRIVATE KEY"},
}};

constexpr Text BEGIN = "-----BEGIN ";
constexpr Text END = "-----END ";
constexpr Text DASHES = "-----";
constexpr Text PROC_TYPE = "Proc-Type: 4,ENCRYPTED";
constexpr std::size_t LINE_LENGTH = 64;

[[noreturn]] void no_clause() { erlang_error(Reason::function_clause); }

bool atom_is(const Term &term, const Text name) { return term.is_atom() && term.atom_spelling_unchecked() == name; }

std::span<const std::byte> bytes_of(const Text text) { return std::as_bytes(std::span(text)); }

Text text_of(const std::vector<std::byte> &bytes) {
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

// The label of a BEGIN line (pem_end/1 is not undefined), or none.
const Label *begin_label(const Text line) {
    if (line.size() < BEGIN.size() + DASHES.size() || !line.starts_with(BEGIN) || !line.ends_with(DASHES)) {
        return nullptr;
    }
    const auto name = line.substr(BEGIN.size(), line.size() - BEGIN.size() - DASHES.size());
    for (const auto &label : LABELS) {
        if (label.label_ == name) {
            return &label;
        }
    }
    return nullptr;
}

// The type term of an entry.
Term type_term(asn1::Builder &builder, const Label &label) {
    if (label.type_.empty()) {
        return builder.tuple({builder.atom("no_asn1"), builder.atom("new_openssh")});
    }
    return builder.atom(label.type_);
}

// One line of the text and the offset after its line break.
struct Line {
    Text text_;
    std::size_t next_ = 0;
};

// binary:split/3 on CRLF, CR and LF: CRLF is one break; the text after the last break is a line, maybe empty.
std::vector<Line> split_lines(const Text text) {
    std::vector<Line> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n' && text[i] != '\r') {
            continue;
        }
        const auto end = i;
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            ++i;
        }
        lines.push_back({text.substr(start, end - start), i + 1});
        start = i + 1;
    }
    lines.push_back({text.substr(start), text.size()});
    return lines;
}

// The white space pubkey_pem strips from the end of a BEGIN line.
Text strip_trailing_space(Text line) {
    while (!line.empty() && Text(" \t\v\f\r\n").find(line.back()) != Text::npos) {
        line.remove_suffix(1);
    }
    return line;
}

// string:tokens/2: the runs of characters outside the separators.
std::vector<Text> tokens(const Text text, const Text separators) {
    std::vector<Text> found;
    std::size_t start = 0;
    while (start < text.size()) {
        start = text.find_first_not_of(separators, start);
        if (start == Text::npos) {
            break;
        }
        const auto end = std::min(text.find_first_of(separators, start), text.size());
        found.push_back(text.substr(start, end - start));
        start = end;
    }
    return found;
}

int hex_digit(const char c) {
    const auto lower = static_cast<char>(c | 0x20);
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return lower >= 'a' && lower <= 'f' ? lower - 'a' + 10 : -1;
}

// list_to_integer([D1, D2], 16): two digits, or a sign and one; badarg for anything else.
int hex_pair(const char first, const char second) {
    const auto low = hex_digit(second);
    if (low >= 0 && hex_digit(first) >= 0) {
        return hex_digit(first) * 16 + low;
    }
    if (low >= 0 && (first == '+' || first == '-')) {
        return first == '+' ? low : -low;
    }
    erlang_error(Reason::badarg);
}

// unhex/2 of a DEK-Info salt: an odd digit left is function_clause; a negative byte fails list_to_binary/1.
Term unhex(asn1::Builder &builder, const Text hex) {
    std::vector<std::byte> bytes;
    bool negative = false;
    std::size_t i = 0;
    for (; i + 1 < hex.size(); i += 2) {
        const auto value = hex_pair(hex[i], hex[i + 1]);
        negative = negative || value < 0;
        bytes.push_back(static_cast<std::byte>(value));
    }
    if (i < hex.size()) {
        no_clause();
    }
    if (negative) {
        erlang_error(Reason::badarg);
    }
    return builder.binary(bytes);
}

// Reads the entries of PEM text in pubkey_pem's order, stopping after an ENCRYPTED PRIVATE KEY entry.
class Reader {
  public:
    Reader(asn1::Builder &builder, const Text text) : builder_(builder), text_(text), lines_(split_lines(text)) {}

    Term entries() {
        std::vector<Term> found;
        while (next_ < lines_.size()) {
            const auto *label = begin_label(strip_trailing_space(lines_[next_++].text_));
            if (label == nullptr) {
                continue;
            }
            const auto body = entry_body();
            if (body.size() >= 2 && body[0].text_.starts_with(PROC_TYPE)) {
                found.push_back(legacy_entry(*label, body));
            } else if (label->type_ == "EncryptedPrivateKeyInfo") {
                const auto rest = text_.substr(lines_[next_ - 1].next_);
                return builder_.tuple({builder_.list(found), decoded(body), builder_.binary(bytes_of(rest))});
            } else {
                found.push_back(
                    builder_.tuple({type_term(builder_, *label), decoded(body), builder_.atom("not_encrypted")}));
            }
        }
        return builder_.tuple({builder_.list(found), builder_.atom("done")});
    }

  private:
    // The lines up to an END line, which is passed; function_clause without one, as join_entry/2.
    std::span<const Line> entry_body() {
        const auto first = next_;
        while (next_ < lines_.size() && !lines_[next_].text_.starts_with(END)) {
            ++next_;
        }
        if (next_ == lines_.size()) {
            no_clause();
        }
        return std::span(lines_).subspan(first, next_++ - first);
    }

    // base64:mime_decode/1 of the lines joined.
    Term decoded(const std::span<const Line> lines) {
        std::string joined;
        for (const auto &line : lines) {
            joined += line.text_;
        }
        return encoding::base64_mime_decode(builder_.context(), builder_.binary(bytes_of(joined)));
    }

    Term strings(const std::vector<Text> &texts) {
        std::vector<Term> terms;
        terms.reserve(texts.size());
        for (const auto text : texts) {
            terms.push_back(builder_.string(bytes_of(text)));
        }
        return builder_.list(terms);
    }

    // [_, DekInfo] = string:tokens(Line, ": "), [Cipher, Salt] = string:tokens(DekInfo, ","), each a badmatch.
    std::vector<Text> exactly_two(const std::vector<Text> &texts) {
        if (texts.size() != 2) {
            const auto value = strings(texts);
            erlang_error(Reason::badmatch, &value);
        }
        return texts;
    }

    // {Type, Der, {Cipher, Salt}} of a Proc-Type entry, its DEK-Info line read after the body is decoded.
    Term legacy_entry(const Label &label, const std::span<const Line> body) {
        const auto der = decoded(body.subspan(2));
        const auto dek_info = exactly_two(tokens(body[1].text_, ": "));
        const auto parts = exactly_two(tokens(dek_info[1], ","));
        const auto salt = unhex(builder_, parts[1]);
        return builder_.tuple(
            {type_term(builder_, label), der, builder_.tuple({builder_.string(bytes_of(parts[0])), salt})});
    }

    asn1::Builder &builder_;
    Text text_;
    std::vector<Line> lines_;
    std::size_t next_ = 0;
};

bool is_openssh(const Term &type) {
    return type.is_tuple() && type.tuple_size_unchecked() == 2 && atom_is(type.tuple_element_unchecked(0), "no_asn1") &&
           atom_is(type.tuple_element_unchecked(1), "new_openssh");
}

// The label of an entry's type (pem_start/1), function_clause for a type of none.
Text type_label(const Term &type) {
    if (atom_is(type, "OneAsymmetricKey")) {
        return "PRIVATE KEY";
    }
    for (const auto &label : LABELS) {
        if (label.type_.empty() ? is_openssh(type) : atom_is(type, label.type_)) {
            return label.label_;
        }
    }
    no_clause();
}

void append_chars(std::string &out, const Term &chars);

// One element of a character list: a Latin-1 character, or a nested list or binary.
void append_char(std::string &out, const Term &element) {
    const auto code = small(element.word());
    if (code && *code >= 0 && *code <= 255) {
        out += static_cast<char>(*code);
    } else if (element.is_list() || element.is_binary()) {
        append_chars(out, element);
    } else {
        bad_argument();
    }
}

// The characters ~s prints of an iolist, binary or atom; badarg for others and for characters past Latin-1.
void append_chars(std::string &out, const Term &chars) {
    if (chars.is_atom()) {
        out += chars.atom_spelling_unchecked();
        return;
    }
    if (chars.is_binary()) {
        out += text_of(need(chars.binary_bytes()));
        return;
    }
    auto rest = chars;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        append_char(out, rest.head_unchecked());
    }
    if (rest.is_binary()) {
        append_chars(out, rest);
    } else if (!rest.is_nil()) {
        bad_argument();
    }
}

// The DEK-Info line of {Cipher, Salt}: io_lib:format("DEK-Info: ~s,~s", [Cipher, Hex]).
std::string dek_info(const Term &info) {
    const auto salt = info.tuple_element_unchecked(1);
    std::string line = "DEK-Info: ";
    append_chars(line, info.tuple_element_unchecked(0));
    line += ',';
    if (!salt.is_binary()) {
        bad_argument();
    }
    for (const auto byte : need(salt.binary_bytes())) {
        constexpr Text DIGITS = "0123456789ABCDEF";
        line += DIGITS[std::to_integer<unsigned>(byte) >> 4U];
        line += DIGITS[std::to_integer<unsigned>(byte) & 15U];
    }
    return line;
}

// base64:encode/1 of the data in lines of 64 characters.
std::string base64_lines(ProcessContext &context, const Term &data) {
    const auto encoded = need(encoding::base64_encode(context, data).binary_bytes());
    const auto text = text_of(encoded);
    std::string lines;
    for (std::size_t i = 0; i < text.size(); i += LINE_LENGTH) {
        lines += i == 0 ? "" : "\n";
        lines += text.substr(i, LINE_LENGTH);
    }
    return lines;
}
} // namespace

// pem_decode(Text): pubkey_pem:decode/1 up to an ENCRYPTED PRIVATE KEY entry, its body and the text after it.
Term pem_decode(ProcessContext &context, const Named<ValueRole> &text) {
    if (!text.term.is_binary()) {
        bad_argument();
    }
    asn1::Builder builder(context);
    const auto bytes = need(text.term.binary_bytes());
    Reader reader(builder, text_of(bytes));
    return reader.entries();
}

// pem_entry({Type, Der, not_encrypted} | {Type, Encrypted, {Cipher, Salt}}): encode_pem_entry/1 as a binary.
Term pem_entry(ProcessContext &context, const Named<ValueRole> &entry) {
    const auto &term = entry.term;
    if (!term.is_tuple() || term.tuple_size_unchecked() != 3) {
        no_clause();
    }
    const auto info = term.tuple_element_unchecked(2);
    const auto plain = atom_is(info, "not_encrypted");
    if (!plain && !(info.is_tuple() && info.tuple_size_unchecked() == 2)) {
        no_clause();
    }
    const auto label = type_label(term.tuple_element_unchecked(0));
    std::string pem = std::string(BEGIN) + std::string(label) + std::string(DASHES) + "\n";
    if (!plain) {
        pem += std::string(PROC_TYPE) + "\n" + dek_info(info) + "\n\n";
    }
    pem += base64_lines(context, term.tuple_element_unchecked(1));
    pem += "\n" + std::string(END) + std::string(label) + std::string(DASHES) + "\n\n";
    asn1::Builder builder(context);
    return builder.binary(bytes_of(pem));
}
} // namespace clause::runtime::builtins::public_key
