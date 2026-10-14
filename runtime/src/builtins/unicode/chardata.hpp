#pragma once
#include "encodings.hpp"

// Walking chardata for the unicode conversions (plan step 41Y1, docs/builtins.md#unicode-module): code points from
// nested lists of integers and binaries (binaries in the input encoding, consecutive binaries decoded as one
// stream), stopping at the first character the output cannot take, with OTP's rest term.
namespace clause::runtime::builtins::unicode {
// The result of walking chardata.
struct Conversion {
    enum class Outcome : std::uint8_t { done, error, incomplete };
    // The code points converted before the outcome.
    std::u32string codes;
    Outcome outcome = Outcome::done;
    // The unconverted rest of the input for error and incomplete, in OTP's shape.
    Term rest;
};

// Walk `data` decoding binaries in `input`; with `latin1_output` only codes up to 255 convert. badarg for data that
// is not chardata.
Conversion convert(ProcessContext &context, const Term &data, Encoding input, bool latin1_output);
} // namespace clause::runtime::builtins::unicode
