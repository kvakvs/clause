#include "text.hpp"
#include "typed.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// Source quotes of compile's messages (plan 21 step LIB77E1, docs/builtins.md#compile): clause_compile:quote_source/3
// quotes the line of a message's location as OTP's sys_messages does, with a marker of '^' under its column and
// OTP's quirks (a column past the line's end widens the marker, a location of a line alone has none). compile.erl
// reads the file and its encoding and formats the message. Added for compile's reports.
namespace clause::runtime::builtins {
namespace {
using Location = Named<struct LocationRole>;
using Encoding = Named<struct EncodingRole>;

// The smallest width of the line number column.
constexpr std::size_t LINE_WIDTH = 4;

// A location's line and column; no column for a location of a line alone. badarg for anything else.
struct Position {
    std::int64_t line = 0;
    std::optional<std::int64_t> column;
};

Position position(const Term &location) {
    if (const auto line = small(location.word())) {
        return {.line = *line, .column = std::nullopt};
    }
    const auto parts = location.is_tuple() ? location.tuple_elements() : TermResult<std::vector<Term>>{};
    if (!parts || parts->size() != 2) {
        bad_argument();
    }
    const auto line = small((*parts)[0].word());
    const auto column = small((*parts)[1].word());
    if (!line || !column) {
        bad_argument();
    }
    return {.line = *line, .column = *column};
}

// The index of the first "\n" at or after `from`, or the size of the source.
std::size_t newline(std::span<const std::byte> source, std::size_t from) {
    while (from < source.size() && source[from] != std::byte{'\n'}) {
        ++from;
    }
    return from;
}

// The bytes of line `number` (from 1) up to its "\n" or "\r\n"; none when the source ends before it starts.
std::optional<std::span<const std::byte>> source_line(std::span<const std::byte> source, std::int64_t number) {
    std::size_t at = 0;
    for (std::int64_t line = 1; line < number && at < source.size(); ++line) {
        at = newline(source, at) + 1;
    }
    if (number < 1 || at >= source.size()) {
        return std::nullopt;
    }
    auto end = newline(source, at);
    if (end < source.size() && end > at && source[end - 1] == std::byte{'\r'}) {
        --end;
    }
    return source.subspan(at, end - at);
}

// The characters of a line: bytes as latin1, or UTF-8 up to the first invalid or unfinished character.
std::u32string characters(std::span<const std::byte> bytes, bool latin1) {
    std::u32string text;
    for (std::size_t at = 0; at < bytes.size();) {
        if (latin1) {
            text.push_back(static_cast<char32_t>(bytes[at++]));
        } else if (const auto code = next_utf8(bytes, at)) {
            text.push_back(*code);
        } else {
            break;
        }
    }
    return text;
}

// The marker under columns [start, end): blanks (tabs kept) before start, then '^' up to end from wherever the line
// ran out.
std::u32string marker(std::u32string_view text, std::int64_t start, std::int64_t end) {
    std::u32string result;
    std::int64_t column = 1;
    for (const auto c : text) {
        if (column >= start) {
            break;
        }
        result.push_back(c == U'\t' ? U'\t' : U' ');
        ++column;
    }
    for (; column < end; ++column) {
        result.push_back(U'^');
    }
    return result;
}

// "% " and the line number (or blanks) right-aligned in its column, then "| ".
std::string margin(const std::string &number, std::size_t width) {
    return "% " + std::string(width - std::min(width, number.size()), ' ') + number + "| ";
}

// quote_source(Source, Encoding, Location): "% Line| Text\n", the marker line when the location has a column, and an
// empty line; <<>> when Source has no such line.
TermResult<Term> quote_source(ProcessContext &context, const BinaryArgument &source, const Encoding &encoding,
                              const Location &location) {
    if (!encoding.term.is_atom()) {
        bad_argument();
    }
    const auto at = position(location.term);
    const auto line = source_line(source.bytes, at.line);
    if (!line) {
        return TermFactory(context).binary({});
    }
    const auto text = characters(*line, encoding.term.atom_spelling_unchecked() == "latin1");
    const auto number = std::to_string(at.line);
    const auto width = std::max(LINE_WIDTH, number.size());
    std::string quote = margin(number, width) + utf8(text) + "\n";
    if (at.column) {
        if (const auto under = marker(text, *at.column, *at.column + 1); !under.empty()) {
            quote += margin("", width) + utf8(under) + "\n";
        }
    }
    quote += "\n";
    return TermFactory(context).binary(std::as_bytes(std::span{quote}));
}

constexpr std::array COMPILE_MESSAGE_BUILTINS{
    typed_entry<quote_source>("clause_compile", "quote_source"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> compile_message_builtins() noexcept { return builtins::COMPILE_MESSAGE_BUILTINS; }
} // namespace clause::runtime
