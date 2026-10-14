#pragma once
#include "term_text.hpp"
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// Pids, ports and references (docs/terms.md#pids-and-references, docs/ports.md#identity): an immediate pid or port
// word carries its number; a reference cell carries a number unique within the program run.
namespace clause::runtime::detail {
// The number of a reference; wrong_type for any other term.
TermResult<std::uint64_t> reference_number(const Term &value) noexcept;
// Order two pids, two ports or two references by number, as OTP orders local identities.
TermResult<int> identity_order(const Term &left, const Term &right) noexcept;
// Print a pid as <0.Number.Serial>, a port as #Port<0.Number> and a reference as #Ref<0.N2.N1.N0>, as OTP prints
// local identities.
TermResult<void> print_identity(const Term &value, TextOutput &out);

// Handle references (plan step 41N, docs/terms.md#handle-references): references that also hold one off-heap
// buffer, as OTP's magic references hold a resource. atomics arrays live in them.
struct HandleAccess {
    // Runs on a handle's buffer when its last reference dies, before the buffer is freed (zlib streams, step 41Q).
    using Finalizer = void (*)(std::span<std::byte> bytes) noexcept;
    // A new reference holding a new off-heap buffer of `bytes`, charged to this process like an off-heap binary.
    static TermResult<Term> make(ProcessHeap &heap, std::span<const std::byte> bytes, Finalizer finalizer = nullptr);
    // The reference of `number`: a handle reference again while its buffer lives, else a plain one (binary_to_term).
    static TermResult<Term> reference(ProcessContext &context, std::uint64_t number);
    // The buffer a handle reference holds, valid while the term is; wrong_type for every other term.
    static TermResult<const std::vector<std::byte> *> buffer(const Term &value) noexcept;

  private:
    // Publish a handle reference cell of `number` holding `buffer`, charged to this process like an off-heap binary.
    static TermResult<Term> publish_handle(ProcessHeap &heap, std::uint64_t number,
                                           std::shared_ptr<const std::vector<std::byte>> buffer);
};
} // namespace clause::runtime::detail
