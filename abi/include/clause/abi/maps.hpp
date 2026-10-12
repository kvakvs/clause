#pragma once
#include "v1.hpp"

namespace clause::abi::v1 {
// from_list takes a proper list of {Key, Value} tuples (later keys win); key_at, value_at and iterator take a map and
// a zero-based position in its canonical key order, iterator returning OTP's {Key, Value, Next} chain ending in none.
enum class MapOperation : std::uint8_t {
    make,
    update,
    get,
    contains,
    size,
    test,
    from_list,
    key_at,
    value_at,
    iterator,
    // An update whose source map the compiler proved unread after it (plan 20 step OPT0.5B): an off-heap map may be
    // updated in place, its stub left consumed.
    update_owned
};
// Semantic failures publish an owned offending key/map; infrastructure failures leave output untouched.
enum class MapOutcome : std::uint8_t { success, bad_map, bad_key, failure };
} // namespace clause::abi::v1

// Borrow rooted source-ordered arguments and publish one complete map/result or semantic error payload.
std::uint8_t CLAUSE_map_v1(void *context, std::uint8_t operation, const clause::abi::v1::TermWord *values,
                           std::size_t count, clause::abi::v1::TermWord *output) noexcept;
