#pragma once

// TermFactory constructs numeric values, atoms and immutable containers with checked ownership.
// Identity and callable constructors remain explicit reporting placeholders.
#include <array>
#include <clause/runtime/features.hpp>
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clause::runtime {
class ContextLifetime;
struct RecordDefinition;
struct FunDefinition;

// Largest tuple arity, OTP's MAX_ARITYVAL (2^24 - 1); builtins building larger tuples raise badarg (plan 11 steps
// 37-38), constructors report resource_limit.
inline constexpr std::size_t MAX_TUPLE_ARITY = 16'777'215;

// Bind process-owned constructors; atoms/booleans use the checked runtime-owned spelling table.
// Raw small integers and empty containers remain available through Term::from_word.
class TermFactory final {
  public:
    // Bind a live-context token and borrowed sink without allocating or registering roots.
    explicit TermFactory(ProcessContext &context, DiagnosticSink sink = {}) noexcept;
    // Release the weak context binding; no process storage or roots are retained.
    ~TermFactory() = default;
    // Keep one factory binding; moving transfers it without moving the process.
    TermFactory(TermFactory &&other) noexcept = default;
    TermFactory &operator=(TermFactory &&other) noexcept = default;
    // Forbid accidental copying of the process-bound factory.
    TermFactory(const TermFactory &) = delete;
    TermFactory &operator=(const TermFactory &) = delete;

    // Construct machine-sized or arbitrary-precision integers (optional sign, decimal digits).
    TermResult<Term> integer(std::int64_t value);
    TermResult<Term> integer_decimal(std::string_view value);
    // Construct a finite Erlang float; reject NaN and infinity.
    TermResult<Term> floating(double value);
    // Delegate interning to the context's runtime AtomStorage; boolean uses its true/false entries.
    TermResult<Term> atom(std::string_view utf8);
    TermResult<Term> boolean(bool value);

    // Construct an empty list, a cons with any tail, or a proper list in input order.
    TermResult<Term> nil();
    TermResult<Term> cons(const Term &head, const Term &tail);
    TermResult<Term> list(std::span<const Term> elements);
    // Construct a proper or improper spine with an explicit final tail in one transaction.
    TermResult<Term> list(std::span<const Term> elements, const Term &tail);
    // The same from ABI words, admitting each like Term::from_word; builtins building long lists avoid a Term each.
    TermResult<Term> list_words(std::span<const Word> elements, const Term &tail);
    // Prepend a proper list's elements to tail in reverse order, reading its cells as words (comprehension results).
    TermResult<Term> reverse(const Term &list, const Term &tail);
    // Construct a tuple, including the zero-element tuple, of at most MAX_TUPLE_ARITY elements.
    TermResult<Term> tuple(std::span<const Term> elements);
    // Construct a tuple from ABI words, admitting each like Term::from_word; generated services avoid a Term per field.
    TermResult<Term> tuple_words(std::span<const Word> elements);
    // The same from words that are already this process's terms, read from its heap, registers or literals, so not
    // admitted again (word-level builtins); the caller guarantees it.
    TermResult<Term> tuple_of_words(std::span<const Word> elements);
    TermResult<Term> list_of_words(std::span<const Word> elements, const Term &tail);
    // Construct a map; the last input entry wins for an exactly equal key.
    TermResult<Term> map(std::span<const std::pair<Term, Term>> entries);
    // Copy bytes or an explicitly sized MSB-first sequence of bits.
    TermResult<Term> binary(std::span<const std::byte> bytes);
    TermResult<Term> bitstring(std::span<const std::byte> bytes, std::size_t bit_count);

    // Wrap runtime-issued identities; constructing a term does not spawn/open a resource.
    TermResult<Term> pid(const ProcessIdentity &identity);
    TermResult<Term> port(const PortIdentity &identity);
    TermResult<Term> reference(const ReferenceIdentity &identity);
    // The reference of a program-wide number, as list_to_ref/1 parses it (step 41D); OTP accepts any number.
    TermResult<Term> reference_number(std::uint64_t number);
    // Obtain a fresh unique reference from the owning runtime.
    TermResult<Term> make_reference();
    // Construct a fun of a registered definition with every captured value in capture order.
    TermResult<Term> fun(const FunDefinition &definition, std::span<const Term> captures);
    // The same from ABI words, admitting each like Term::from_word.
    TermResult<Term> fun_words(const FunDefinition &definition, std::span<const Word> captures);
    // Rewrap an extracted callable identity without losing its captured values.
    TermResult<Term> function(const FunctionIdentity &identity);
    // Construct a native record of a registered definition with every field value in definition order.
    TermResult<Term> native_record(const RecordDefinition &definition, std::span<const Term> fields);
    // The same from ABI words, admitting each like Term::from_word.
    TermResult<Term> native_record_words(const RecordDefinition &definition, std::span<const Word> fields);

  private:
    // Reject expired or moved-from bindings before reporting an unavailable constructor.
    TermResult<Term> unavailable(std::string_view operation) const noexcept;
    // Check the weak lifetime before dereferencing the borrowed construction owner.
    TermResult<ProcessHeap *> heap() const noexcept;
    // Construct one proper or improper immutable spine in a single transaction.
    TermResult<Term> list_tail(std::span<const Term> elements, const Term &tail);
    // Inspect context liveness without retaining or dereferencing process storage.
    std::weak_ptr<const ContextLifetime> lifetime_;
    // Borrow diagnostic delivery state for this factory's lifetime; null selects stderr.
    DiagnosticSink sink_;
    // Borrow the runtime table only while the context lifetime token is alive.
    AtomStorage *atoms_;
    // Borrow stable backing only while the process token is alive; moves leave source tokens empty.
    ProcessHeap *heap_;
};
} // namespace clause::runtime
