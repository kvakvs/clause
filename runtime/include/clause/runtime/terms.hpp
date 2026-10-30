#pragma once
#include "base_types.hpp"
#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace clause::runtime {
// Host API failures; these are not Erlang exception terms or generated-code ABI values.
enum class TermError : std::uint8_t {
    wrong_type,
    out_of_range,
    invalid_encoding,
    invalid_argument,
    improper_list,
    missing_key,
    unknown_field,
    wrong_owner,
    expired_context,
    resource_limit,
    out_of_memory,
    not_implemented,
    diagnostic_failure,
    // An integer beyond the ERTS size limit; Erlang code sees error:system_limit.
    system_limit
};

struct TermTag {
    // Keep the encoded word intact; C++ bitfield order never defines the term ABI.
    Word value_;

    // Resolve the first non-delegating tag, including immediate empty tuples and lists.
    [[nodiscard]] constexpr TermKind get_kind() const noexcept {
        static constexpr std::array kinds{
            TermKind::header,    TermKind::list,         TermKind::boxed,       TermKind::invalid,
            TermKind::local_pid, TermKind::local_port,   TermKind::invalid,     TermKind::smallint,
            TermKind::atom,      TermKind::catch_object, TermKind::empty_tuple, TermKind::empty_list,
        };
        const auto primary = static_cast<unsigned>(value_ & abi::v1::primary_mask);
        const auto secondary = static_cast<unsigned>((value_ >> 2) & 3U);
        const auto tertiary = static_cast<unsigned>((value_ >> 4) & 3U);
        const auto use_secondary = static_cast<unsigned>(primary == 3U);
        const auto use_tertiary = use_secondary & static_cast<unsigned>(secondary == 2U);
        // Delegation advances index 3 to row 4, then index 6 to row 8; ignored fields contribute zero.
        const auto index = primary + use_secondary * (1U + secondary) + use_tertiary * (4U + tertiary - secondary);
        return kinds[index];
    }
};

static_assert(sizeof(TermTag) == sizeof(Word));
static_assert(alignof(TermTag) == alignof(Word));
static_assert((static_cast<unsigned>(TermKind2::smallint) << 2 |
               static_cast<unsigned>(TermKindPrimary::see_termkind2)) == abi::v1::small_integer_tag);

// Carry a checked value or failure without fabricating an Erlang result.
template <typename Value> using TermResult = std::expected<Value, TermError>;

// Inspect immediate tag structure only; atom/pid/port payloads do not establish runtime identity.
// Headers, catches and noncanonical empty values are invalid; list/boxed words return wrong_type.
[[nodiscard]] TermResult<TermKind> classify_immediate(Word value) noexcept;

// Encode a native-width small integer; values requiring bignums fail with out_of_range.
[[nodiscard]] TermResult<Word> encode_integer(std::int64_t value) noexcept;

// Decode only small integers; malformed immediates are invalid_encoding, other categories wrong_type.
[[nodiscard]] TermResult<std::int64_t> decode_integer(Word value) noexcept;
class ProcessContext;
class ContextLifetime;
class GeneratedCallState;
class ProcessHeap;
class AtomStorage;
struct AtomValue;
class ProcessIdentity;

// The identity of a port (docs/ports.md#identity): its number, kept outside any heap and rebuilt as a term by
// TermFactory::port. Obtained only from a port term (Term::port_value).
class PortIdentity final {
  public:
    // Order by number, as ports order.
    auto operator<=>(const PortIdentity &) const noexcept = default;

  private:
    friend class Term;
    friend class TermFactory;

    // Wrap the number of an existing port.
    explicit PortIdentity(const Word number) noexcept : number_(number) {}

    // The process-wide port number.
    Word number_;
};

// The identity of a reference: the number make_reference/0 issued, kept outside any heap (monitor references) and
// rebuilt as a term by TermFactory::reference. Obtained only from a reference term (Term::reference_value).
class ReferenceIdentity final {
  public:
    // Order by number, as references order.
    auto operator<=>(const ReferenceIdentity &) const noexcept = default;

  private:
    friend class Term;
    friend class TermFactory;

    // Wrap the number of an existing reference.
    explicit ReferenceIdentity(const std::uint64_t number) noexcept : number_(number) {}

    // The program-wide reference number.
    std::uint64_t number_;
};

// A function identity outside fun terms; none exist, so TermFactory::function reports term services unavailable.
class FunctionIdentity final {};
class TermFactory;

namespace detail {
class HeapStorage;
struct TermAccess;
struct BitAccess;
} // namespace detail

// Whether contextual admission (Term::from_word with a context, factory and service inputs) checks a word's range,
// shape and owner: Debug builds only. Release builds trust every word, as ERTS does (OPTM.2C).
#ifdef NDEBUG
inline constexpr bool CHECKED_ADMISSION = false;
#else
inline constexpr bool CHECKED_ADMISSION = true;
#endif

// Identify an atom within its runtime; word-sized IDs remain stable across future storage compaction.
using AtomId = std::uintptr_t;

// Host value: one tagged word, valid until its heap's next collection or its process's exit (ERTS Eterm held by C
// code). Nothing is pinned or checked per handle; a Term held past either reads freed or moved memory.
class Term final {
  public:
    // Reserve zero as an invalid slot until checked immediate construction supplies a value.
    Term() : value_(0) {}

    // Copy the word; rebinding a handle leaves every other alias unchanged.
    Term(const Term &other) = default;
    Term(Term &&other) noexcept = default;
    Term &operator=(const Term &other) = default;
    Term &operator=(Term &&other) noexcept = default;
    ~Term() = default;

    // Admit only small integers and canonical empty containers; identities/heap values remain unavailable.
    static TermResult<Term> from_word(Word value) noexcept;
    // Admit atoms and published compound starts only with proof of runtime/process ownership.
    static TermResult<Term> from_word(Word value, ProcessContext &context) noexcept;
    // Expose the one-word representation for the generated service bridge.
    Word word() const noexcept;

    // Return this value as a term of destination; see ProcessHeap::add.
    TermResult<Term> copy_to(ProcessHeap &destination) const noexcept;

    // Remaining semantic/heap operations below are reserved unless documented as implemented.

    // Identify the semantic category; binaries are byte-sized bitstrings.
    TermKind kind() const;

    // Test numeric categories without exposing small-integer/bignum storage.
    bool is_integer() const;
    bool is_float() const;
    bool is_number() const;
    // Test atoms, including the true/false convenience subset.
    bool is_atom() const;
    bool is_boolean() const;
    // Test opaque identity and callable categories.
    bool is_reference() const;
    bool is_function() const;
    bool is_function(std::size_t arity) const;
    bool is_port() const;
    bool is_pid() const;
    // The local tests above see only this node's identities; these see another node's (plan 30 step DIST1).
    bool is_external_pid() const;
    bool is_external_port() const;
    bool is_external_reference() const;
    // Test container categories without exposing their storage.
    bool is_tuple() const;
    bool is_map() const;
    bool is_nil() const;
    bool is_cons() const;
    // Match Erlang is_list/1 (nil or cons); properness is a separate traversal.
    bool is_list() const;
    TermResult<bool> is_proper_list() const;
    // Test bitstrings and their whole-byte subset.
    bool is_bitstring() const;
    bool is_binary() const;
    // Distinguish native records from traditional tuple-backed records.
    bool is_native_record() const;

    // Extract bounded or lossless decimal integer values; narrowing checks range.
    TermResult<std::int64_t> integer_value() const;
    TermResult<std::string> integer_decimal() const;
    // Extract a float without coercing an integer.
    TermResult<double> float_value() const;
    // Copy the atom's Unicode spelling as UTF-8, or extract true/false.
    TermResult<std::string> atom_utf8() const;
    // Borrow validated UTF-8 bytes from the atom table, valid until the runtime is torn down.
    TermResult<std::string_view> atom_spelling() const noexcept;
    // Extract the atom's stable runtime-local number, not an integer Term or a storage address.
    TermResult<AtomId> atom_id() const;
    TermResult<bool> boolean_value() const;

    // Extract opaque identities, never process pointers, numeric IDs or native callbacks.
    TermResult<ProcessIdentity> pid_value() const;
    TermResult<PortIdentity> port_value() const;
    TermResult<ReferenceIdentity> reference_value() const;
    TermResult<FunctionIdentity> function_value() const;
    // Inspect callable arity without invoking it or exposing its environment storage.
    TermResult<std::size_t> function_arity() const;

    // Extract a cons cell; tail may be any term, including an improper-list tail.
    TermResult<Term> head() const;
    TermResult<Term> tail() const;
    // Traverse a proper list; report improper_list for a non-nil terminal tail.
    TermResult<std::size_t> list_length() const;
    TermResult<std::vector<Term>> list_elements() const;
    // Grow a list by returning a new value in the same process context.
    TermResult<Term> prepend(const Term &element) const;
    TermResult<Term> append(const Term &element) const;
    // Replace a zero-based element in a proper list, preserving the original.
    TermResult<Term> with_list_element(std::size_t index, const Term &element) const;

    // Inspect tuple size and elements using zero-based C++ indices.
    TermResult<std::size_t> tuple_size() const;
    TermResult<Term> tuple_element(std::size_t index) const;
    TermResult<std::vector<Term>> tuple_elements() const;
    // Replace one tuple element without modifying existing aliases.
    TermResult<Term> with_tuple_element(std::size_t index, const Term &element) const;

    // Inspect maps with exact Erlang key equality; absence is an empty optional.
    TermResult<std::size_t> map_size() const;
    TermResult<bool> map_contains(const Term &key) const;
    TermResult<std::optional<Term>> map_find(const Term &key) const;
    TermResult<std::vector<std::pair<Term, Term>>> map_entries() const;
    // Insert/replace (=>), replace-existing-only (:=), or remove a key immutably.
    TermResult<Term> with_map_entry(const Term &key, const Term &value) const;
    TermResult<Term> with_existing_map_entry(const Term &key, const Term &value) const;
    TermResult<Term> without_map_entry(const Term &key) const;

    // Copy packed MSB-first bits; unused low bits in the final byte are zero.
    TermResult<std::size_t> bit_size() const;
    TermResult<std::vector<std::byte>> bitstring_bytes() const;
    // Extract bytes only if the bitstring is a binary.
    TermResult<std::vector<std::byte>> binary_bytes() const;
    // Return a checked bit slice or concatenation in the same process context.
    TermResult<Term> bit_slice(std::size_t offset, std::size_t count) const;
    TermResult<Term> concat_bits(const Term &suffix) const;

    // Read a native record's field by atom name (unknown_field when absent), or every field name and value in
    // definition order.
    TermResult<Term> record_field(const Term &name) const;
    TermResult<std::vector<std::pair<Term, Term>>> record_fields() const;

    // Compare values using Erlang exact equality, not C++ handle identity.
    TermResult<bool> exactly_equal(const Term &other) const;

    // Unchecked accessors (OPTM.3): the caller has already proved the kind (and an index in range), so nothing is
    // checked and no error is possible; any other term is undefined behavior. Each mirrors the checked accessor named
    // without the suffix.
    // A small integer's value (Term::integer_value of a non-bignum).
    std::int64_t small_integer_unchecked() const noexcept;
    double float_value_unchecked() const noexcept;
    // A live atom's spelling and number, and true/false as a bool.
    std::string_view atom_spelling_unchecked() const noexcept;
    AtomId atom_id_unchecked() const noexcept;
    bool boolean_value_unchecked() const noexcept;
    PortIdentity port_value_unchecked() const noexcept;
    ReferenceIdentity reference_value_unchecked() const noexcept;
    // A cons cell's fields.
    Term head_unchecked() const noexcept;
    Term tail_unchecked() const noexcept;
    // A tuple's arity and zero-based element (the empty tuple included).
    std::size_t tuple_size_unchecked() const noexcept;
    Term tuple_element_unchecked(std::size_t index) const noexcept;
    std::size_t map_size_unchecked() const noexcept;
    std::size_t bit_size_unchecked() const noexcept;

  private:
    friend class TermFactory;
    friend class AtomStorage;
    friend class GeneratedCallState;
    friend struct detail::TermAccess;

    // Replace a process-root Term's word after a collection moved it.
    void rebind(Word value) noexcept;

    //=============================================================
    // This is a **hard requirement**, Term is one word in size.
    // The user must explicitly confirm that growing the size of Term is intended.
    // Usually it never is.
    //=============================================================

    // Store the ABI word; an atom's spelling is read from the process-wide atom table by its number. This is the
    // only field: a Term is exactly one Word, any further data lives in its tag bits or in a boxed object (OPTM.2).
    Word value_;
};

static_assert(sizeof(Term) == sizeof(Word), "a Term is exactly one Word");
static_assert(std::is_trivially_copyable_v<Term>, "a Term is trivially copyable");

} // namespace clause::runtime
