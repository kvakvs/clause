#pragma once
#include "support.hpp"
#include <cstdint>
#include <optional>
#include <string>

// io:format/1,2 and io:put_chars/1 text (docs/libraries/io.md): OTP's io_lib_format and io_lib_pretty rules applied by
// the standard output device, producing characters that the device writes as UTF-8.
namespace clause::runtime::builtins {
// The characters of `format` with each control sequence replaced, as io_lib:format(Format, Args) (no `arguments`:
// io:format/1, Args = []); throws BuiltinFailure (badarg for format errors and text that is not Unicode).
std::u32string format_text(ProcessContext &context, const Term &format, std::optional<Term> arguments);

// unicode:characters_to_list(Chardata): code points, binaries of UTF-8 and nested lists ending in [] or a binary;
// throws BuiltinFailure for anything else.
std::u32string characters(const Term &chardata);

} // namespace clause::runtime::builtins
