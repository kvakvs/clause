#include "../memory/heap_object.hpp"
#include "integers.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <sstream>

namespace clause::runtime::detail {
std::string integer_text(const Integer &value) {
    // Machine-word integers print without a stream (conversion builtins call this for every integer).
    if (value >= std::numeric_limits<std::int64_t>::min() && value <= std::numeric_limits<std::int64_t>::max()) {
        std::array<char, 24> digits{};
        const auto written =
            std::to_chars(digits.data(), digits.data() + digits.size(), value.convert_to<std::int64_t>());
        return {digits.data(), written.ptr};
    }
    std::ostringstream output;
    output.exceptions(std::ios_base::badbit | std::ios_base::failbit);
    output << value;
    return output.str();
}

std::size_t integer_bits(const Integer &value) noexcept {
    const auto &backend = value.backend();
    constexpr auto limb_bits = std::numeric_limits<boost::multiprecision::limb_type>::digits;
    return (backend.size() - 1) * limb_bits + std::bit_width(backend.limbs()[backend.size() - 1]);
}

TermResult<Integer> integer_read(const Term &value) {
    if (value.kind() == TermKind::smallint) {
        return Integer(value.integer_value().value());
    }
    const auto object = TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::bignum) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto words = object->words;
    return integer_words(words.subspan(2), words[1] != 0);
}

TermResult<Integer> integer_parse(std::string_view text) {
    const bool negative = text.starts_with('-');
    if (negative || text.starts_with('+')) {
        text.remove_prefix(1);
    }
    if (text.empty()) {
        return std::unexpected(TermError::invalid_argument);
    }
    if (!std::ranges::all_of(text, [](char digit) { return digit >= '0' && digit <= '9'; })) {
        return std::unexpected(TermError::invalid_argument);
    }
    const auto first = text.find_first_not_of('0');
    text = first == std::string_view::npos ? std::string_view{"0"} : text.substr(first);
    // More digits than the largest magnitude has never fit; one more bit is caught when the value is stored.
    if (text.size() > integer_decimal_limit) {
        return std::unexpected(TermError::system_limit);
    }
    Integer result = integer_digits(text);
    if (negative) {
        result = -result;
    }
    return result;
}
} // namespace clause::runtime::detail
