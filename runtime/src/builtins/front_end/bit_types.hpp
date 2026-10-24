#pragma once
#include "../support.hpp"
#include <clause/abi/bits.hpp>
#include <optional>
#include <string_view>

// The type specifiers of a bin_element as erl_bits:set_bit_type/2 settles them (plan 21 steps LIB31, LIB32): shared by
// erl_parse:normalise/1 and erl_eval's binaries.
namespace clause::runtime::builtins::front_end {
// The size and type specifiers of a bin_element as written.
struct BitSpecifiers {
    Term size;
    Term types;
};

// A bin_element's specifiers with erl_bits' defaults applied.
struct BitTypeSpec {
    abi::v1::BitType type = abi::v1::BitType::integer;
    // The unit; none for the UTF types, which have no unit.
    std::optional<unsigned> unit;
    bool signed_value = false;
    // big, little or native as written (big by default).
    std::string_view endian = "big";
    // The default size when none is written: 8 for integers, 64 for floats, none for binaries (all) and UTF types.
    std::optional<std::int64_t> default_size;
};

// set_bit_type(Size, Types) for a Size that is `default` or an abstract expression: erl_bits' errors are raised as
// error(Reason) ({undefined_bittype, T}, {bittype_mismatch, ...}, bittype_unit, utf_bittype_size_or_unit).
BitTypeSpec settle_bit_type(ProcessContext &context, const BitSpecifiers &written);

// Whether a settled type is one of utf8, utf16 and utf32.
inline bool is_utf_type(abi::v1::BitType type) {
    return type == abi::v1::BitType::utf8 || type == abi::v1::BitType::utf16 || type == abi::v1::BitType::utf32;
}
} // namespace clause::runtime::builtins::front_end
