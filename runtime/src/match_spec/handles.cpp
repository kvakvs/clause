#include "../terms/identities.hpp"
#include "program.hpp"
#include <array>
#include <cstring>
#include <type_traits>

// Compiled match specifications as terms (plan 21 step LIB11): ets:match_spec_compile/1 gives a handle reference (an
// OTP magic reference) whose buffer holds a program; it survives term_to_binary/1 while the program lives, as OTP 20+
// magic references do, and the program goes with the last reference.
namespace clause::runtime::detail::match_spec {
namespace {
// The buffer of a compiled specification's handle.
struct Stored {
    // Tells a compiled specification apart from other handles.
    std::uint64_t magic;
    // The program, owned by the handle.
    std::shared_ptr<const Program> *program;
};

static_assert(std::is_trivially_copyable_v<Stored>);

constexpr std::uint64_t MAGIC = 0x6D617463685F7370; // "match_sp"

void finalize(std::span<std::byte> bytes) noexcept {
    Stored stored{};
    std::memcpy(&stored, bytes.data(), sizeof(stored));
    delete stored.program;
}
} // namespace

std::shared_ptr<const Program> program_of(const Term &term) {
    if (!term.is_reference()) {
        return nullptr;
    }
    const auto buffer = HandleAccess::buffer(term);
    if (!buffer || (*buffer)->size() != sizeof(Stored)) {
        return nullptr;
    }
    Stored stored{};
    std::memcpy(&stored, (*buffer)->data(), sizeof(stored));
    return stored.magic == MAGIC ? *stored.program : nullptr;
}

TermResult<Term> handle(ProcessContext &context, std::shared_ptr<const Program> program) {
    const Stored stored{.magic = MAGIC, .program = new std::shared_ptr<const Program>(std::move(program))};
    std::array<std::byte, sizeof(Stored)> bytes{};
    std::memcpy(bytes.data(), &stored, sizeof(stored));
    auto made = HandleAccess::make(context.heap(), bytes, finalize);
    if (!made) {
        delete stored.program;
    }
    return made;
}
} // namespace clause::runtime::detail::match_spec
