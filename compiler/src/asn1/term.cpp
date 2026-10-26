#include "model.hpp"
#include <algorithm>
#include <array>

// Added for the ASN.1 compiler (plan 21 step LIB84): Erlang term text in io_lib's `~w` form.
namespace clause::asn1 {
namespace {
constexpr std::array ERLANG_RESERVED = {"after",   "and",  "andalso", "band",  "begin", "bnot", "bor",  "bsl",
                                        "bsr",     "bxor", "case",    "catch", "cond",  "div",  "else", "end",
                                        "fun",     "if",   "let",     "maybe", "not",   "of",   "or",   "orelse",
                                        "receive", "rem",  "try",     "when",  "xor"};

bool atom_char(const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '@';
}

bool bare_atom(const std::string_view name) {
    if (name.empty() || name.front() < 'a' || name.front() > 'z' || !std::ranges::all_of(name, atom_char)) {
        return false;
    }
    return std::ranges::find(ERLANG_RESERVED, name) == ERLANG_RESERVED.end();
}

void print_into(const Term &term, std::string &out);

void print_items(const std::vector<Term> &items, std::string &out) {
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out += ',';
        }
        print_into(items[i], out);
    }
}

// <<B1,...,Bn>> or <<B1,...,Last:Bits>> for a bitstring whose length is not a multiple of 8.
void print_bits(const Term &term, std::string &out) {
    out += "<<";
    const auto whole = term.kind_ == Term::Kind::binary ? term.text_.size() : term.bits_ / 8;
    for (std::size_t i = 0; i < whole; ++i) {
        if (i != 0) {
            out += ',';
        }
        out += std::to_string(static_cast<unsigned char>(term.text_[i]));
    }
    const auto rest = term.kind_ == Term::Kind::binary ? 0 : term.bits_ % 8;
    if (rest != 0) {
        if (whole != 0) {
            out += ',';
        }
        const auto byte = static_cast<unsigned char>(term.text_[whole]);
        out += std::to_string(byte >> (8 - rest)) + ":" + std::to_string(rest);
    }
    out += ">>";
}

void print_map(const Term &term, std::string &out) {
    out += "#{";
    for (std::size_t i = 0; i + 1 < term.items_.size(); i += 2) {
        if (i != 0) {
            out += ',';
        }
        print_into(term.items_[i], out);
        out += "=>";
        print_into(term.items_[i + 1], out);
    }
    out += '}';
}

void print_into(const Term &term, std::string &out) {
    switch (term.kind_) {
    case Term::Kind::atom:
        out += atom_text(term.text_);
        break;
    case Term::Kind::integer:
    case Term::Kind::real:
        out += term.text_;
        break;
    case Term::Kind::tuple:
        out += '{';
        print_items(term.items_, out);
        out += '}';
        break;
    case Term::Kind::list:
        out += '[';
        print_items(term.items_, out);
        out += ']';
        break;
    case Term::Kind::binary:
    case Term::Kind::bitstring:
        print_bits(term, out);
        break;
    case Term::Kind::map:
        print_map(term, out);
        break;
    }
}
} // namespace

Term Term::string(const std::string_view text) {
    std::vector<Term> codes;
    codes.reserve(text.size());
    for (const char c : text) {
        codes.push_back(integer(static_cast<unsigned char>(c)));
    }
    return list(std::move(codes));
}

std::string atom_text(const std::string_view name) {
    if (bare_atom(name)) {
        return std::string(name);
    }
    std::string out = "'";
    for (const char c : name) {
        if (c == '\'' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "'";
}

std::string print(const Term &term) {
    std::string out;
    print_into(term, out);
    return out;
}
} // namespace clause::asn1
