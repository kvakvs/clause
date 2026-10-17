#include "../terms/identities.hpp"
#include "../terms/integers.hpp"
#include "typed.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <span>
#include <terms.hpp>
#include <vector>

// The atomics module (plan step 41N, docs/builtins.md#atomics-and-counters): an array of 64-bit atomic integers in
// the off-heap buffer of a handle reference, shared by every copy of the reference and freed with its last one.
// Word 0 of the buffer records the signedness; the values follow. Every operation is sequentially consistent.
namespace clause::runtime::builtins {
namespace {
using detail::Integer;

// compare_exchange/4's desired value, an integer apart from the expected one.
struct Desired {
    Integer value;
};
} // namespace

// Any integer as compare_exchange/4's desired value.
template <> struct Argument<Desired> {
    static TermResult<std::optional<Desired>> convert(const Term &term) {
        return Argument<detail::Integer>::convert(term).transform([](std::optional<detail::Integer> value) {
            return value ? std::optional{Desired{std::move(*value)}} : std::nullopt;
        });
    }
};

namespace {

// Word 0 of an array: signed or unsigned values.
constexpr std::uint64_t SIGNED_ARRAY = 1;
constexpr std::uint64_t UNSIGNED_ARRAY = 2;
// Bytes OTP reports for an array besides its values (atomics:info/1 memory).
constexpr std::int64_t ARRAY_OVERHEAD_BYTES = 40;
// The most values one array holds (Clause's limit; OTP has none below its allocator), keeping sizes in a word.
constexpr std::int64_t MAX_ARITY = std::int64_t{1} << 27;

// The atom `name`.
Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// The array of an atomics reference; badarg for any other term.
struct Array {
    // The values, after the signedness word.
    std::span<std::uint64_t> values;
    bool is_signed;
};

// The array a reference holds; badarg unless it is an atomics reference.
Array array(const Term &reference) {
    const auto buffer = detail::HandleAccess::buffer(reference);
    if (!buffer) {
        bad_argument();
    }
    // The buffer is shared mutable storage written only through std::atomic_ref; it is never read as a binary.
    auto *words = reinterpret_cast<std::uint64_t *>(const_cast<std::byte *>((*buffer)->data()));
    const auto count = (*buffer)->size() / sizeof(std::uint64_t);
    if (count == 0 || (words[0] != SIGNED_ARRAY && words[0] != UNSIGNED_ARRAY)) {
        bad_argument();
    }
    return {std::span(words + 1, count - 1), words[0] == SIGNED_ARRAY};
}

// The value at a 1-based index; badarg outside the array.
std::atomic_ref<std::uint64_t> at(const Array &values, std::int64_t index) {
    if (index < 1 || static_cast<std::uint64_t>(index) > values.values.size()) {
        bad_argument();
    }
    return std::atomic_ref<std::uint64_t>(values.values[static_cast<std::size_t>(index - 1)]);
}

// The stored bits of `value` when it fits the array's range; badarg otherwise.
std::uint64_t stored(const Array &values, const Integer &value) {
    if (values.is_signed
            ? value < std::numeric_limits<std::int64_t>::min() || value > std::numeric_limits<std::int64_t>::max()
            : value < 0 || value > std::numeric_limits<std::uint64_t>::max()) {
        bad_argument();
    }
    return value < 0 ? static_cast<std::uint64_t>(static_cast<std::int64_t>(value)) : static_cast<std::uint64_t>(value);
}

// The bits an increment adds, wrapping: any integer from -2^63 to 2^64 - 1; badarg otherwise.
std::uint64_t increment(const Integer &value) {
    if (value < std::numeric_limits<std::int64_t>::min() || value > std::numeric_limits<std::uint64_t>::max()) {
        bad_argument();
    }
    return value < 0 ? static_cast<std::uint64_t>(static_cast<std::int64_t>(value)) : static_cast<std::uint64_t>(value);
}

// Stored bits as the array's integer.
TermResult<Term> value_term(ProcessContext &context, const Array &values, std::uint64_t bits) {
    const auto value = values.is_signed ? Integer(static_cast<std::int64_t>(bits)) : Integer(bits);
    return detail::IntegerAccess::make(context.heap(), value);
}

// The spelling of an atom; empty for any other term.
std::string_view spelling(const Term &term) {
    return term.is_atom() ? term.atom_spelling_unchecked() : std::string_view{};
}

// The flag of one {signed, Bool} option; badarg for anything else.
bool signed_option(const Term &option) {
    if (!option.is_tuple() || option.tuple_size_unchecked() != 2 ||
        spelling(option.tuple_element_unchecked(0)) != "signed") {
        bad_argument();
    }
    const auto flag = spelling(option.tuple_element_unchecked(1));
    if (flag != "true" && flag != "false") {
        bad_argument();
    }
    return flag == "true";
}

// The signedness an option list asks for: [{signed, Bool}], the last one winning; badarg for anything else.
std::uint64_t signedness(const Term &options) {
    auto kind = SIGNED_ARRAY;
    auto rest = options;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto option = rest.head_unchecked();
        kind = signed_option(option) ? SIGNED_ARRAY : UNSIGNED_ARRAY;
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return kind;
}

// new(Arity, Options): a new array of Arity zeros.
TermResult<Term> new_array(ProcessContext &context, std::int64_t arity, const Term &options) {
    if (arity < 1 || arity > MAX_ARITY) {
        bad_argument();
    }
    std::vector<std::uint64_t> words(static_cast<std::size_t>(arity) + 1, 0);
    words[0] = signedness(options);
    return detail::HandleAccess::make(context.heap(), std::as_bytes(std::span(words)));
}

// get(Ref, Ix).
TermResult<Term> get(ProcessContext &context, const Term &reference, std::int64_t index) {
    const auto values = array(reference);
    return value_term(context, values, at(values, index).load());
}

// put(Ref, Ix, Value).
Term put(ProcessContext &context, const Term &reference, std::int64_t index, const Integer &value) {
    const auto values = array(reference);
    at(values, index).store(stored(values, value));
    return atom(context, "ok");
}

// add(Ref, Ix, Incr): wraps around.
Term add(ProcessContext &context, const Term &reference, std::int64_t index, const Integer &value) {
    const auto values = array(reference);
    at(values, index).fetch_add(increment(value));
    return atom(context, "ok");
}

// add_get(Ref, Ix, Incr): the new value.
TermResult<Term> add_get(ProcessContext &context, const Term &reference, std::int64_t index, const Integer &value) {
    const auto values = array(reference);
    const auto bits = increment(value);
    return value_term(context, values, at(values, index).fetch_add(bits) + bits);
}

// sub(Ref, Ix, Decr).
Term sub(ProcessContext &context, const Term &reference, std::int64_t index, const Integer &value) {
    const auto values = array(reference);
    at(values, index).fetch_sub(increment(value));
    return atom(context, "ok");
}

// sub_get(Ref, Ix, Decr): the new value.
TermResult<Term> sub_get(ProcessContext &context, const Term &reference, std::int64_t index, const Integer &value) {
    const auto values = array(reference);
    const auto bits = increment(value);
    return value_term(context, values, at(values, index).fetch_sub(bits) - bits);
}

// exchange(Ref, Ix, Desired): the old value.
TermResult<Term> exchange(ProcessContext &context, const Term &reference, std::int64_t index, const Integer &value) {
    const auto values = array(reference);
    return value_term(context, values, at(values, index).exchange(stored(values, value)));
}

// compare_exchange(Ref, Ix, Expected, Desired): ok when the value was Expected and is now Desired, else the value.
TermResult<Term> compare_exchange(ProcessContext &context, const Term &reference, std::int64_t index,
                                  const Integer &expected_value, const Desired &desired) {
    const auto values = array(reference);
    auto expected = stored(values, expected_value);
    if (at(values, index).compare_exchange_strong(expected, stored(values, desired.value))) {
        return atom(context, "ok");
    }
    return value_term(context, values, expected);
}

// info(Ref): #{size, max, min, memory}.
TermResult<Term> info(ProcessContext &context, const Term &reference) {
    const auto values = array(reference);
    const auto size = static_cast<std::int64_t>(values.values.size());
    const auto max = values.is_signed ? Integer(std::numeric_limits<std::int64_t>::max())
                                      : Integer(std::numeric_limits<std::uint64_t>::max());
    const auto min = values.is_signed ? Integer(std::numeric_limits<std::int64_t>::min()) : Integer(0);
    const auto integer = [&](const Integer &value) { return need(detail::IntegerAccess::make(context.heap(), value)); };
    const std::array entries{
        std::pair{atom(context, "size"), integer(Integer(size))},
        std::pair{atom(context, "max"), integer(max)},
        std::pair{atom(context, "min"), integer(min)},
        std::pair{atom(context, "memory"), integer(Integer(ARRAY_OVERHEAD_BYTES + 8 * size))},
    };
    return TermFactory(context).map(entries);
}
} // namespace

constexpr std::array ATOMICS_BUILTINS{
    typed_entry<new_array>("atomics", "new"),
    typed_entry<get>("atomics", "get"),
    typed_entry<put>("atomics", "put"),
    typed_entry<add>("atomics", "add"),
    typed_entry<add_get>("atomics", "add_get"),
    typed_entry<sub>("atomics", "sub"),
    typed_entry<sub_get>("atomics", "sub_get"),
    typed_entry<exchange>("atomics", "exchange"),
    typed_entry<compare_exchange>("atomics", "compare_exchange"),
    typed_entry<info>("atomics", "info"),
};
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> atomics_builtins() noexcept { return builtins::ATOMICS_BUILTINS; }
} // namespace clause::runtime
