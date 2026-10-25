#pragma once
#include "deep.hpp"
#include "write.hpp"
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// io_lib_pretty (plan 21 step LIB4D, docs/builtins.md#io-lib): io_lib:print/1,4, the ~p and ~P controls and the
// chars_limit forms of io_lib:write/2 and write_bin/5, built as OTP's intermediate format and laid out in OTP's deep
// list shapes (or as one UTF-8 binary for io_lib:bformat/2,3 and bwrite/2).
namespace clause::runtime::builtins::io_lib {
// A print depth that is not an integer: comparisons with it fail and arithmetic on it raises badarith, as in OTP.
inline constexpr std::int64_t NON_INTEGER_DEPTH = std::numeric_limits<std::int64_t>::min();

// A record definition known to the printer, standing for a record_print_fun answer: a tuple {Name, V1, ..., Vn}
// with as many fields prints as #Name{F1 = V1, ...}. Added for the shell's record printing (plan 21 step LIB74).
struct RecordDefinition {
    Word name = 0;
    std::vector<Term> fields{};
};

using RecordDefinitions = std::vector<RecordDefinition>;

// The options of io_lib_pretty:print/2; record_print_fun is given as its definitions (`records`).
struct PrettyOptions {
    // Column of the first character, from 1; smaller columns count as 1.
    std::int64_t column = 1;
    // The line length the layout keeps within; 0 writes one line.
    std::int64_t line_length = 80;
    // The print depth (-1 unlimited) and chars_limit (-1 unlimited).
    std::int64_t depth = -1;
    std::int64_t chars_limit = -1;
    // line_max_chars: at most this many characters on a line, -1 for the term's one-line length.
    std::int64_t line_max_chars = -1;
    // Atoms in Latin-1 or Unicode; binaries read as UTF-8 under unicode.
    Encoding encoding = Encoding::latin1;
    // Printable lists and binaries print as strings.
    bool strings = true;
    MapsOrder order = MapsOrder::undefined;
    // Whether column, line_length and depth are integers: OTP's arithmetic on them raises badarith otherwise.
    bool numeric = true;
    // The record definitions tuples print with; none prints every tuple as a tuple (record_print_fun no_fun).
    const RecordDefinitions *records = nullptr;
};

// io_lib_pretty:print/2 as chars().
Deep::Ref pretty_print(ProcessContext &context, Deep &deep, const Term &term, const PrettyOptions &options);

// io_lib_pretty:print_bin/2's {Utf8, Size, Column}: Column > 0 is the column after the text, otherwise its negated
// width on the starting line.
struct PrettyBinary {
    std::string bytes{};
    std::int64_t size = 0;
    std::int64_t column = 0;
};

PrettyBinary pretty_print_bin(ProcessContext &context, const Term &term, const PrettyOptions &options);

// io_lib:write/5 and write_bin/5 with chars_limit above 0: intermediate/7 (strings off) written on one line.
struct LimitedWrite {
    Encoding encoding = Encoding::latin1;
    MapsOrder order = MapsOrder::undefined;
    std::int64_t depth = -1;
    std::int64_t chars_limit = 1;
};

Deep::Ref limited_write(ProcessContext &context, Deep &deep, const Term &term, const LimitedWrite &options);
// The same as UTF-8 and the intermediate format's length.
std::pair<std::string, std::int64_t> limited_write_bin(ProcessContext &context, const Term &term,
                                                       const LimitedWrite &options);

// io_lib:print/1's characters (crash reports print exit reasons with it).
std::u32string pretty_text(ProcessContext &context, const Term &term);
} // namespace clause::runtime::builtins::io_lib
