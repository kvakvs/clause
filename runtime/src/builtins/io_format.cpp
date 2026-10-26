#include "io_format.hpp"
#include "support.hpp"
#include "text.hpp"
#include <vector>

// Chardata read as characters for io:put_chars/1,2 and io:format's output (docs/libraries/io.md); the format engine
// itself is io_lib's (io_lib/format_build.cpp).
namespace clause::runtime::builtins {
namespace {
// Append one character of a chardata leaf, or throw badarg for a code that is not a Unicode character.
void append_character(const Term &leaf, std::u32string &out) {
    const auto code = small(leaf.word());
    if (!code || !unicode_character(*code)) {
        bad_argument();
    }
    out.push_back(static_cast<char32_t>(*code));
}

// Append the characters of a UTF-8 binary chardata element.
void append_binary(const Term &binary, std::u32string &out) {
    if (!binary.is_binary()) {
        bad_argument();
    }
    const auto text = decode_utf8(need(binary.binary_bytes()));
    if (!text) {
        bad_argument();
    }
    out += *text;
}

// Read one list until its end or a nested list, which is queued to be read before the rest of this one.
void read_chardata(Term list, std::u32string &out, std::vector<Term> &pending) {
    while (list.is_cons()) {
        auto element = list.head_unchecked();
        list = list.tail_unchecked();
        if (element.is_list()) {
            pending.push_back(list);
            pending.push_back(element);
            return;
        }
        element.is_binary() ? append_binary(element, out) : append_character(element, out);
    }
    if (!list.is_nil()) {
        append_binary(list, out);
    }
}
} // namespace

std::u32string characters(const Term &chardata_term) {
    std::u32string out;
    if (!chardata_term.is_list()) {
        append_binary(chardata_term, out);
        return out;
    }
    // Chardata lists are usually printed lines, past the 3-character small-string buffer.
    out.reserve(64);
    std::vector<Term> pending{chardata_term};
    while (!pending.empty()) {
        auto list = pending.back();
        pending.pop_back();
        read_chardata(list, out, pending);
    }
    return out;
}
} // namespace clause::runtime::builtins
