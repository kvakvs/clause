#pragma once
#include "status.hpp"
#include "v1.hpp"
#include <cstddef>
#include <type_traits>

namespace clause::abi::v1 {
struct RecordDescriptor;
struct FunDescriptor;
struct FrameDescriptor;

struct AtomDescriptor {
    // Borrow exact UTF-8 bytes; empty atoms may use a null pointer with zero length.
    const char *spelling;
    std::size_t size;
};

struct ExportDescriptor {
    // Borrow exact UTF-8 bytes; registration copies metadata before returning.
    const char *name;
    std::size_t name_size;
    // Resolve the generic entry's fixed Erlang arity independently of its machine signature.
    std::size_t arity;
    // Null with a null frame for an export the executable never calls (plan step 62C): only its name remains, for
    // function_exported/3, and dynamic calls of it raise undef.
    GeneratedFunction *entry;
    // The FrameDescriptor dynamic calls (M:F(Args), apply/3) enter; null for a host-only export.
    const FrameDescriptor *frame = nullptr;
};

struct ModuleDescriptor {
    // Reject incompatible layouts before reading any borrowed metadata.
    std::uint32_t abi_version;
    std::uint32_t term_bits;
    // Borrow exact module spelling and a contiguous, immutable export table.
    const char *name;
    std::size_t name_size;
    const ExportDescriptor *exports;
    std::size_t export_count;
    // Bind compiler-assigned slots to runtime-owned words before module publication.
    const AtomDescriptor *atoms = nullptr;
    std::size_t atom_count = 0;
    // Native record definitions of this module (records.hpp), bound to its atom slots at registration.
    const RecordDescriptor *records = nullptr;
    std::size_t record_count = 0;
    // Function values this module creates (funs.hpp), bound to its atom slots at registration.
    const FunDescriptor *funs = nullptr;
    std::size_t fun_count = 0;
    // Writable words beside the descriptor: the runtime identity that claimed them (0 for none), then the word of
    // every atom slot. The first runtime to register the module fills them, so its generated code loads atom
    // literals without a call; others call CLAUSE_atom_v3 (OPT0.8). Null for a descriptor without one.
    TermWord *atom_cache = nullptr;
};

struct FrameDescriptor {
    // Name one generated function in stack traces: its module (null leaves the frame out of traces), the atom
    // slots spelling the module and function names, and its Erlang arity. Immutable, beside the module descriptor.
    const ModuleDescriptor *module;
    std::size_t module_atom;
    std::size_t function_atom;
    std::size_t arity;
    // Code continuing the function at its frame's resume index: after entry and after every callee returns.
    Code *body;
    // Words after the frame header; the first `roots` hold terms (arguments first), the rest raw spilled values.
    std::size_t slots;
    std::size_t roots;
};

// Explicit startup entry; the pointer must designate an active runtime owner.
using GeneratedRegistration = std::uint8_t(void *);
static_assert(std::is_standard_layout_v<ExportDescriptor>);
static_assert(std::is_standard_layout_v<ModuleDescriptor>);
static_assert(sizeof(ExportDescriptor) == 5 * sizeof(TermWord));
static_assert(offsetof(ExportDescriptor, entry) == 3 * sizeof(TermWord));
static_assert(sizeof(AtomDescriptor) == 2 * sizeof(TermWord));
static_assert(sizeof(ModuleDescriptor) == 8 + 11 * sizeof(TermWord));
static_assert(std::is_standard_layout_v<FrameDescriptor>);
static_assert(sizeof(FrameDescriptor) == 7 * sizeof(TermWord));
static_assert(offsetof(ModuleDescriptor, name) == 8);
static_assert(offsetof(ModuleDescriptor, exports) == 8 + 2 * sizeof(TermWord));
} // namespace clause::abi::v1

// Generated service boundary uses native C++ linkage with a fixed, documented linker spelling.
// Borrow a Runtime and ModuleDescriptor; contain all exceptions and return a v1::Status byte.
std::uint8_t CLAUSE_register_module_v4(void *runtime, const void *descriptor) noexcept;

// Load an initialized module slot for this context's runtime; failures enter the checked error channel.
clause::abi::v1::TermWord CLAUSE_atom_v3(void *context, std::size_t slot, const void *descriptor) noexcept;
