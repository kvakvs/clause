#pragma once
#include "term_layout.hpp"
#include "term_text.hpp"
#include <array>
#include <byte_buffer.hpp>
#include <clause/runtime/terms.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

// Pids, ports and references (docs/terms.md#pids-and-references, docs/ports.md#identity): an immediate pid or port
// word carries its number; a reference cell carries a number unique within the program run.
namespace clause::runtime::detail {
// The number of a reference; wrong_type for any other term.
TermResult<std::uint64_t> reference_number(const Term &value) noexcept;
// Whether a term is a pid, port or reference of this node or another one.
bool is_identity(const Term &value) noexcept;
// Order two pids, two ports or two references of any nodes as OTP orders them: pids by serial, number and then
// node; ports and references by node and then numbers. A node orders by name, then creation.
TermResult<int> identity_order(const Term &left, const Term &right) noexcept;
// Print a pid as <C.Number.Serial>, a port as #Port<C.Number> and a reference as #Ref<C.Wn...W0>, as OTP prints
// identities: C is 0 for this node and the node name atom's number for another node.
TermResult<void> print_identity(const Term &value, TextOutput &out);

// This node's name and creation (plan 30 step DIST1, docs/distribution.md): nonode@nohost and 0 until
// distribution starts. Process-wide, like the atom table.
std::string_view this_node_name() noexcept;
std::uint32_t this_node_creation() noexcept;
// This node's name as an atom of `context`.
TermResult<Term> this_node_atom(ProcessContext &context);

// An identity of another node (plan 30 step DIST1, docs/terms.md#identities-of-other-nodes), as its cell holds it.
struct ExternalIdentity final {
    // external_pid, external_port or external_reference.
    TermKind kind = TermKind::external_pid;
    // The node name atom's word and the node's creation.
    Word node = 0;
    std::uint32_t creation = 0;
    // A pid's number and serial, a port's number low word first, or a reference's words low first; `count` in use.
    std::array<std::uint32_t, layout::MAX_EXTERNAL_NUMBERS> numbers{};
    std::size_t count = 0;
};

// Building and reading identities of other nodes (plan 30 step DIST1).
struct ExternalAccess {
    // A new cell of `identity`; the node atom must be a live atom.
    static TermResult<Term> make(ProcessHeap &heap, const ExternalIdentity &identity);
    // The parts of an identity of another node; wrong_type for any other term.
    static TermResult<ExternalIdentity> read(const Term &value) noexcept;
};

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
    static TermResult<const ByteBuffer *> buffer(const Term &value) noexcept;

  private:
    // Publish a handle reference cell of `number` holding `buffer`, charged to this process like an off-heap binary.
    static TermResult<Term> publish_handle(ProcessHeap &heap, std::uint64_t number,
                                           std::shared_ptr<const ByteBuffer> buffer);
};
} // namespace clause::runtime::detail
