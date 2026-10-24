#include "compilation.hpp"
#include "limits.hpp"
#include "llvm_state.hpp"
#include <stdexcept>
#include <utility>

namespace clause::codegen {
namespace detail {
CompilationState &state(Compilation &compilation) {
    if (!compilation.state_) {
        throw std::logic_error("compilation owner has been moved or consumed");
    }
    return *compilation.state_;
}

const CompilationState &state(const Compilation &compilation) {
    if (!compilation.state_) {
        throw std::logic_error("compilation owner has been moved or consumed");
    }
    return *compilation.state_;
}

CompilationState::CompilationState(CompilationRequest owned_request)
    : request(std::move(owned_request)), context(std::make_unique<llvm::LLVMContext>()) {
    // Respect LLVM opt-in remark filters while retaining ordinary warnings and errors.
    validate_input_limits(request.inputs, request.limits);
    context->setDiagnosticHandlerCallBack(capture_diagnostic, &result, true);
    modules.reserve(request.inputs.size());
    for (const auto &input : request.inputs) {
        const auto path = input.source_path.generic_u8string();
        const std::string identifier(path.begin(), path.end());
        modules.push_back(std::make_unique<llvm::Module>(identifier, *context));
    }
}
} // namespace detail

Compilation::Compilation(CompilationRequest request)
    : state_(std::make_unique<detail::CompilationState>(std::move(request))) {}

Compilation::~Compilation() = default;
Compilation::Compilation(Compilation &&) noexcept = default;
Compilation &Compilation::operator=(Compilation &&) noexcept = default;

const CompilationRequest &Compilation::request() const { return detail::state(*this).request; }

CompilationResult &Compilation::result() { return detail::state(*this).result; }

const CompilationResult &Compilation::result() const { return detail::state(*this).result; }

CompilationResult Compilation::take_result() && {
    auto &state = detail::state(*this);
    state.modules.clear();
    state.module_contexts.clear();
    state.target_machine.reset();
    state.context.reset();
    auto result = std::move(state.result);
    state_.reset();
    return result;
}
} // namespace clause::codegen
