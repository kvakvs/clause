#include "../memory/heap_object.hpp"
#include "../process/identities.hpp"
#include "atom_table.hpp"
#include "bitstrings.hpp"
#include "term_layout.hpp"
#include <clause/abi/equality.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The unchecked accessors (OPTM.3): each reads the words its kind has, trusting the caller's proof of that kind.
namespace clause::runtime {
using detail::TermAccess;

namespace {
// Debug builds stop at a broken proof of kind instead of reading the wrong words (CHECKED_ADMISSION).
void expect(bool proven, const char *accessor) noexcept {
    if (CHECKED_ADMISSION && !proven) {
        std::fprintf(stderr, "unchecked accessor %s on a term of another kind\n", accessor);
        std::abort();
    }
}
} // namespace

std::int64_t Term::small_integer_unchecked() const noexcept {
    expect(kind() == TermKind::smallint, "small_integer_unchecked");
    // An arithmetic shift drops the tag and keeps the sign.
    return static_cast<std::int64_t>(static_cast<std::intptr_t>(value_) >> abi::v1::small_integer_bits);
}

double Term::float_value_unchecked() const noexcept {
    expect(is_float(), "float_value_unchecked");
    double value = 0;
    std::memcpy(&value, TermAccess::trusted(value_) + 1, sizeof(value));
    return value;
}

std::string_view Term::atom_spelling_unchecked() const noexcept {
    expect(is_atom(), "atom_spelling_unchecked");
    return detail::atom_table::find(value_)->spelling;
}

AtomId Term::atom_id_unchecked() const noexcept {
    expect(is_atom(), "atom_id_unchecked");
    return value_ >> abi::v1::atom_tag_bits;
}

bool Term::boolean_value_unchecked() const noexcept {
    expect(is_boolean(), "boolean_value_unchecked");
    return value_ == abi::v1::true_atom;
}

PortIdentity Term::port_value_unchecked() const noexcept {
    expect(is_port(), "port_value_unchecked");
    return PortIdentity(detail::port_number(value_));
}

ReferenceIdentity Term::reference_value_unchecked() const noexcept {
    expect(is_reference(), "reference_value_unchecked");
    std::uint64_t number = 0;
    std::memcpy(&number, TermAccess::trusted(value_) + 1, sizeof(number));
    return ReferenceIdentity(number);
}

Term Term::head_unchecked() const noexcept {
    expect(is_cons(), "head_unchecked");
    return TermAccess::make(TermAccess::trusted(value_)[0]);
}

Term Term::tail_unchecked() const noexcept {
    expect(is_cons(), "tail_unchecked");
    return TermAccess::make(TermAccess::trusted(value_)[1]);
}

std::size_t Term::tuple_size_unchecked() const noexcept {
    expect(is_tuple(), "tuple_size_unchecked");
    return value_ == abi::v1::empty_tuple ? 0 : detail::layout::BoxHeader::count(TermAccess::trusted(value_)[0]);
}

Term Term::tuple_element_unchecked(std::size_t index) const noexcept {
    expect(is_tuple() && index < tuple_size_unchecked(), "tuple_element_unchecked");
    return TermAccess::make(TermAccess::trusted(value_)[index + 1]);
}

std::size_t Term::map_size_unchecked() const noexcept {
    expect(is_map(), "map_size_unchecked");
    return TermAccess::object(*this)->count;
}

std::size_t Term::bit_size_unchecked() const noexcept {
    expect(is_bitstring(), "bit_size_unchecked");
    const auto view = detail::trusted_bit_view(value_);
    return view ? view->length : 0;
}
} // namespace clause::runtime
