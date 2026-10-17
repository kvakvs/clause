#include "deep.hpp"
#include "../io_format.hpp"
#include "../string/string.hpp"
#include "../text.hpp"
#include <optional>

namespace clause::runtime::builtins::io_lib {
namespace {
using string::small_of;
using string::Terms;

// One step of io_lib_format:indentation_bin/5: OTP's Start and N (which a tab leaves counting from the binary's
// start) and the column.
struct BinaryColumn {
    std::size_t start = 0;
    std::size_t n = 0;
    std::int64_t column = 0;
};

// string:length(binary:part(Orig, Start, N)); badarg when the part is outside the binary.
std::int64_t part_length(ProcessContext &context, const Term &binary, const BinaryColumn &at) {
    const auto size = string::binary_bytes(binary).size();
    if (at.start > size || at.n > size - at.start) {
        bad_argument();
    }
    const auto part = Terms(context).slice(binary, at.start, at.n);
    return *small_of(string::length_of(context, part));
}

// io_lib_format:indentation_bin/2.
std::int64_t binary_indentation(ProcessContext &context, const Term &binary, std::int64_t column) {
    BinaryColumn at{.column = column};
    const auto size = string::binary_bytes(binary).size();
    for (std::size_t i = 0; i < size; ++i) {
        const auto byte = std::to_integer<char>(string::binary_bytes(binary)[i]);
        if (byte == '\n') {
            at = {.start = at.n + 1, .n = 0, .column = 0};
        } else if (byte == '\t') {
            const auto width = part_length(context, binary, at);
            at = {.start = at.n + 1, .n = at.n + 1, .column = (at.column + width + 8) / 8 * 8};
        } else {
            ++at.n;
        }
    }
    return at.column + part_length(context, binary, at);
}

// The column after one character.
std::int64_t next_column(char32_t code, std::int64_t column) {
    return code == '\n' ? 0 : code == '\t' ? (column + 8) / 8 * 8 : column + 1;
}

// The iolist size of one list element: one for a byte, none for an integer that is not one.
std::optional<std::int64_t> element_size(const Term &element) {
    if (!element.is_integer()) {
        return term_iolist_size(element);
    }
    const auto byte = small_of(element);
    return byte && *byte >= 0 && *byte <= 255 ? std::optional<std::int64_t>{1} : std::nullopt;
}

// The iolist size of a list, none when it is not an iolist.
std::optional<std::int64_t> list_iolist_size(Term list) {
    std::int64_t size = 0;
    for (; list.is_cons(); list = list.tail_unchecked()) {
        const auto part = element_size(list.head_unchecked());
        if (!part) {
            return std::nullopt;
        }
        size += *part;
    }
    if (list.is_nil()) {
        return size;
    }
    const auto tail = list.is_binary() ? term_iolist_size(list) : std::nullopt;
    return tail ? std::optional{size + *tail} : std::nullopt;
}

// One list level of deep_list_of/2: nested lists queued, false at an integer `accept` refuses or an improper end.
bool list_level(Term rest, bool (*accept)(std::int64_t), std::vector<Term> &pending) {
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        if (head.is_list()) {
            pending.push_back(head);
            continue;
        }
        const auto value = small_of(head);
        if (!value || !accept(*value)) {
            return false;
        }
    }
    return rest.is_nil();
}
} // namespace

bool unicode_char(std::int64_t c) {
    return (c >= 0 && c < 0xD800) || (c > 0xDFFF && c < 0xFFFE) || (c > 0xFFFF && c <= 0x10FFFF);
}

bool deep_list_of(const Term &term, bool (*accept)(std::int64_t)) {
    if (!term.is_list()) {
        return false;
    }
    std::vector<Term> pending{term};
    while (!pending.empty()) {
        const auto rest = pending.back();
        pending.pop_back();
        if (!list_level(rest, accept, pending)) {
            return false;
        }
    }
    return true;
}

std::optional<std::int64_t> term_iolist_size(const Term &data) {
    if (data.is_binary()) {
        return static_cast<std::int64_t>(string::binary_bytes(data).size());
    }
    return data.is_list() ? list_iolist_size(data) : std::nullopt;
}

std::int64_t term_chars_length(ProcessContext &context, const Term &data) {
    if (const auto size = term_iolist_size(data)) {
        return *size;
    }
    return *small_of(string::length_of(context, data));
}

std::int64_t term_indentation(ProcessContext &context, const Term &data, std::int64_t column) {
    Term rest = data;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto head = rest.head_unchecked();
        if (const auto code = small_of(head)) {
            column = next_column(static_cast<char32_t>(*code), column);
        } else if (head.is_integer()) {
            ++column;
        } else {
            column = term_indentation(context, head, column);
        }
    }
    if (rest.is_binary()) {
        return binary_indentation(context, rest, column);
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return column;
}

Deep::Deep() { cells_.push_back({.kind = Kind::nil, .first = 0, .second = 0}); }

Deep::Ref Deep::add(Cell cell) {
    cells_.push_back(cell);
    return Ref{static_cast<std::uint32_t>(cells_.size() - 1)};
}

Deep::Ref Deep::code(char32_t code) { return add({.kind = Kind::code, .first = code, .second = 0}); }

Deep::Ref Deep::cons(Ref head, Ref tail) {
    return add(
        {.kind = Kind::cons, .first = static_cast<std::uint32_t>(head), .second = static_cast<std::uint32_t>(tail)});
}

Deep::Ref Deep::text(std::u32string_view codes, Ref tail) {
    for (auto it = codes.rbegin(); it != codes.rend(); ++it) {
        tail = cons(code(*it), tail);
    }
    return tail;
}

Deep::Ref Deep::ascii(std::string_view chars, Ref tail) {
    for (auto it = chars.rbegin(); it != chars.rend(); ++it) {
        tail = cons(code(static_cast<unsigned char>(*it)), tail);
    }
    return tail;
}

Deep::Ref Deep::list(std::initializer_list<Ref> items, Ref tail) { return list(std::span(items), tail); }

Deep::Ref Deep::list(std::span<const Ref> items, Ref tail) {
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        tail = cons(*it, tail);
    }
    return tail;
}

Deep::Ref Deep::term(const Term &term) {
    terms_.push_back(term);
    return add({.kind = Kind::term, .first = static_cast<std::uint32_t>(terms_.size() - 1), .second = 0});
}

Term Deep::to_term(ProcessContext &context, Ref ref) const {
    const Terms terms(context);
    switch (kind(ref)) {
    case Kind::nil:
        return terms.nil();
    case Kind::code:
        return terms.integer(code_of(ref));
    case Kind::term:
        return term_of(ref);
    case Kind::cons:
        break;
    }
    std::vector<Term> items;
    for (; kind(ref) == Kind::cons; ref = tail(ref)) {
        items.push_back(to_term(context, head(ref)));
    }
    return terms.list(items, to_term(context, ref));
}

void Deep::flatten(Ref ref, std::u32string &out) const {
    for (; kind(ref) == Kind::cons; ref = tail(ref)) {
        flatten(head(ref), out);
    }
    if (kind(ref) == Kind::code) {
        out.push_back(code_of(ref));
    } else if (kind(ref) == Kind::term) {
        // A character kept as its term (string_char/4's printable characters) is one code point.
        const auto code = character_of(ref);
        if (term_of(ref).is_integer() && (!code || !unicode_character(*code))) {
            bad_argument();
        }
        if (code) {
            out.push_back(static_cast<char32_t>(*code));
        } else {
            out += characters(term_of(ref));
        }
    }
}

std::u32string Deep::flat(Ref ref) const {
    std::u32string out;
    flatten(ref, out);
    return out;
}

std::optional<std::int64_t> Deep::character_of(Ref ref) const {
    if (kind(ref) == Kind::code) {
        return code_of(ref);
    }
    return kind(ref) == Kind::term && term_of(ref).is_integer() ? small_of(term_of(ref)) : std::nullopt;
}

std::optional<std::int64_t> Deep::item_size(Ref item) const {
    if (const auto c = character_of(item)) {
        return *c >= 0 && *c <= 255 ? std::optional<std::int64_t>{1} : std::nullopt;
    }
    return kind(item) == Kind::term && term_of(item).is_integer() ? std::nullopt : iolist_size(item);
}

std::optional<std::int64_t> Deep::iolist_size(Ref ref) const {
    std::int64_t size = 0;
    for (; kind(ref) == Kind::cons; ref = tail(ref)) {
        const auto part = item_size(head(ref));
        if (!part) {
            return std::nullopt;
        }
        size += *part;
    }
    if (kind(ref) == Kind::nil) {
        return size;
    }
    const auto tail_size = kind(ref) == Kind::term ? term_iolist_size(term_of(ref)) : std::nullopt;
    return tail_size ? std::optional{size + *tail_size} : std::nullopt;
}

std::int64_t Deep::chars_length(ProcessContext &context, Ref ref) const {
    if (const auto size = iolist_size(ref)) {
        return *size;
    }
    return *small_of(string::length_of(context, to_term(context, ref)));
}

std::int64_t Deep::indentation(ProcessContext &context, Ref ref, std::int64_t column) const {
    for (; kind(ref) == Kind::cons; ref = tail(ref)) {
        const auto item = head(ref);
        const auto c = character_of(item);
        column = c ? next_column(static_cast<char32_t>(*c), column) : indentation(context, item, column);
    }
    if (kind(ref) == Kind::term) {
        return term_indentation(context, term_of(ref), column);
    }
    return column;
}
} // namespace clause::runtime::builtins::io_lib
