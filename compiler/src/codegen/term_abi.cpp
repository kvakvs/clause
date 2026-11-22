#include "term_abi.hpp"
#include "llvm_state.hpp"
#include <cassert>
#include <llvm/IR/DerivedTypes.h>

namespace clause::codegen {
llvm::IntegerType *term_type(Compilation &compilation) {
    auto &state = detail::state(compilation);
    if (state.result.status() != CompilationStatus::incomplete) {
        return nullptr;
    }
    // Lowering configures the target first (VAL13: an assert, not a test).
    assert(state.target_machine != nullptr);
    if (!state.target_machine) {
        state.result.report({.level = DiagnosticLevel::error,
                             .message = "Term ABI requires a configured target machine",
                             .location = {},
                             .module_name = {}});
        return nullptr;
    }
    const auto layout = state.target_machine->createDataLayout();
    const auto bits = layout.getPointerSizeInBits();
    if (bits != 32 && bits != 64) {
        state.result.report({.level = DiagnosticLevel::error,
                             .message = "Term ABI requires 32-bit or 64-bit target words",
                             .location = {},
                             .module_name = {}});
        return nullptr;
    }
    auto *type = llvm::IntegerType::get(*state.context, bits);
    if (layout.getABITypeAlign(type).value() != bits / 8 || layout.getPointerABIAlignment(0).value() != bits / 8 ||
        layout.isNonIntegralAddressSpace(0)) {
        state.result.report({.level = DiagnosticLevel::error,
                             .message = "Term ABI requires integral word-aligned pointers and terms",
                             .location = {},
                             .module_name = {}});
        return nullptr;
    }
    return type;
}

llvm::FunctionType *generated_function_type(Compilation &compilation) {
    auto *word = term_type(compilation);
    if (word == nullptr) {
        return nullptr;
    }
    auto *pointer = llvm::PointerType::get(*detail::state(compilation).context, 0);
    return llvm::FunctionType::get(word, {pointer, pointer}, false);
}
} // namespace clause::codegen
