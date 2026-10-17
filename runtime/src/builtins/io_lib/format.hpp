#pragma once
#include "deep.hpp"
#include <optional>
#include <string>
#include <variant>
#include <vector>

// io_lib's format engine (plan 21 step LIB4C, docs/builtins.md#io-lib): io_lib_format's scan, build_small and
// build_limited over format specs, so io_lib:format/2,3 return OTP's deep lists and io:format writes them.
namespace clause::runtime::builtins::io_lib {
// One control sequence (io_lib:format_spec()).
struct Spec {
    // control_char and the arguments it took.
    char32_t control = 0;
    std::vector<Term> args;
    // width (none or a count) and adjust (left when true).
    std::optional<std::int64_t> width;
    bool left = false;
    std::optional<std::int64_t> precision;
    // pad_char: any term ($\s by default, the format's or an argument's otherwise).
    Term pad;
    // encoding unicode (the t modifier) and strings (false with l).
    bool unicode = false;
    bool strings = true;
    // maps_order: undefined, ordered (k), or the K modifier's argument.
    Term order;
};

// One element of a scanned format: a literal (a character or nested chardata, as it is) or a control.
using Element = std::variant<Term, Spec>;

// io_lib_format:scan/2 when it consumes every argument; errors as OTP raises them inside io_lib.
std::vector<Element> scan(ProcessContext &context, const Term &format, const Term &arguments);
// The elements of a scanned format list (io_lib:build_text/1's input).
std::vector<Element> elements_of(ProcessContext &context, const Term &format_list);
// The format_spec() map of a control.
Term spec_term(ProcessContext &context, const Spec &spec);
// io_lib_format:unscan/1.
Term unscan(ProcessContext &context, const Term &format_list);

// io_lib_format:build/2 with chars_limit `limit` (-1: none); `binary` for build_bin/2, whose limited pass takes only
// characters, binaries and lists starting a line as literals.
Deep::Ref build(ProcessContext &context, Deep &deep, const std::vector<Element> &elements, const Term &limit,
                bool binary = false);
// get_option/3: the value of the first {Key, Value} of an options list (lists:keyfind/3), else `fallback`.
Term option_of(const Term &options, std::string_view key, const Term &fallback);

// A float control's value and precision.
struct FloatField {
    double value = 0;
    std::int64_t precision = 6;
};

// io_lib_format's float controls: the ~e text with `precision` digits (none below 2) and the ~f text with
// `precision` decimals (none below 1), as float_e/3 and float_f/3 round OTP's 21 digits.
std::optional<std::string> float_e(const FloatField &field);
std::optional<std::string> float_f(const FloatField &field);

// fwrite_g/5's choice for `precision` significant digits: ~f with these decimals, or ~e with these digits.
struct FloatChoice {
    bool fixed = false;
    std::int64_t precision = 0;
};

FloatChoice float_g(const FloatField &field);
} // namespace clause::runtime::builtins::io_lib
