#include <array>
#include <clause/abi/term.hpp>
#include <clause/runtime/terms.hpp>

namespace clause::runtime {
namespace {
// The kind of a low six-bit tag: primary 0-2 decide alone, primary 3 delegates to the secondary field, whose value 2
// delegates to the tertiary field. Added for VAL13: the tag table checked at compile time instead of by a test.
constexpr TermKind expected_kind(const Word tag) {
    constexpr std::array primary{TermKind::header, TermKind::list, TermKind::boxed};
    constexpr std::array secondary{TermKind::local_pid, TermKind::local_port, TermKind::invalid, TermKind::smallint};
    constexpr std::array tertiary{TermKind::atom, TermKind::catch_object, TermKind::empty_tuple, TermKind::empty_list};
    if ((tag & 3U) != 3U) {
        return primary[tag & 3U];
    }
    const auto field = (tag >> 2) & 3U;
    return field == 2U ? tertiary[(tag >> 4) & 3U] : secondary[field];
}

// Whether TermTag::get_kind resolves all 64 combinations of the three tag fields as the table above.
constexpr bool tag_table_holds() {
    for (Word tag = 0; tag < 64; ++tag) {
        if (TermTag{tag}.get_kind() != expected_kind(tag)) {
            return false;
        }
    }
    return true;
}

static_assert(tag_table_holds(), "TermTag::get_kind disagrees with the tag table");
} // namespace

TermResult<TermKind> classify_immediate(Word value) noexcept {
    const auto kind = TermTag{value}.get_kind();
    switch (kind) {
    case TermKind::header:
    case TermKind::catch_object:
        return std::unexpected(TermError::invalid_encoding);
    case TermKind::list:
    case TermKind::boxed:
        return std::unexpected(TermError::wrong_type);
    case TermKind::empty_tuple:
    case TermKind::empty_list:
        if ((value >> 6) != 0) {
            return std::unexpected(TermError::invalid_encoding);
        }
        return kind;
    default:
        return kind;
    }
}

TermResult<Word> encode_integer(std::int64_t value) noexcept {
    return abi::v1::NativeIntegerEncoding::encode(value).transform_error(
        [](abi::v1::IntegerError) { return TermError::out_of_range; });
}

TermResult<std::int64_t> decode_integer(Word value) noexcept {
    const auto kind = classify_immediate(value);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    return abi::v1::NativeIntegerEncoding::decode(value).transform_error(
        [](abi::v1::IntegerError) { return TermError::wrong_type; });
}
} // namespace clause::runtime
