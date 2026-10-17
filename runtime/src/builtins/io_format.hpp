#pragma once
#include "support.hpp"
#include <cstdint>
#include <optional>
#include <string>

// io:format/1,2 and io:put_chars/1 text (docs/io.md): OTP's io_lib_format and io_lib_pretty rules applied by the
// standard output device, producing characters that the device writes as UTF-8.
namespace clause::runtime::builtins {
// The characters of `format` with each control sequence replaced, as io_lib:format(Format, Args) (no `arguments`:
// io:format/1, Args = []); throws BuiltinFailure (badarg for format errors and text that is not Unicode).
std::u32string format_text(ProcessContext &context, const Term &format, std::optional<Term> arguments);

// unicode:characters_to_list(Chardata): code points, binaries of UTF-8 and nested lists ending in [] or a binary;
// throws BuiltinFailure for anything else.
std::u32string characters(const Term &chardata);

// The options of one ~p control sequence.
struct PrettyOptions {
    // Column of the first character, from 1; the field's precision or the current column.
    std::int64_t column = 1;
    // Line length the layout tries to stay within; the field width, 80 by default, 0 for one line.
    std::int64_t line_length = 80;
    // The t modifier: atoms keep characters beyond Latin-1 and UTF-8 binaries print as "..."/utf8.
    bool unicode = false;
    // Without the l modifier, printable lists and binaries print as strings.
    bool strings = true;
};

// io_lib_pretty:print/2 of `term`; throws BuiltinFailure.
std::u32string pretty(const Term &term, const PrettyOptions &options);
} // namespace clause::runtime::builtins
