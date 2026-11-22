#include "codegen/verification.hpp"
#include "codegen/llvm_state.hpp"
#include "codegen/target.hpp"
#include <iostream>
#include <llvm/IR/IRBuilder.h>
#include <stdexcept>
#include <string>
#include <utility>

using namespace clause;
using namespace clause::codegen;

// Keep verifier assertions active in both debug and release test builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Retain two modules so an invalid later module must invalidate the entire batch.
CompilationRequest request() {
    CompilationRequest batch;
    batch.inputs.emplace_back("first.erl", ast::Module{});
    batch.inputs.emplace_back("second.erl", ast::Module{});
    return batch;
}

// Build a synthetic constant-return function without claiming Erlang lowering or ABI support.
llvm::Function &add_function(llvm::Module &module) {
    llvm::IRBuilder<> builder(module.getContext());
    auto *type = llvm::FunctionType::get(builder.getInt32Ty(), false);
    auto *function = llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, "answer", module);
    builder.SetInsertPoint(llvm::BasicBlock::Create(module.getContext(), "entry", function));
    builder.CreateRet(builder.getInt32(42));
    return *function;
}

// Configure real target layouts before constructing the tiny IR fixtures in their owned context.
Compilation synthetic_batch() {
    Compilation compilation(request());
    require(configure_target(compilation), "target configuration failed");
    for (auto &module : detail::state(compilation).modules) {
        add_function(*module);
    }
    return compilation;
}

// Valid bodies and external declarations verify repeatedly without completing or emitting the batch.
void check_valid() {
    auto compilation = synthetic_batch();
    auto &module = *detail::state(compilation).modules.front();
    auto *type = llvm::FunctionType::get(llvm::Type::getVoidTy(module.getContext()), false);
    llvm::Function::Create(type, llvm::GlobalValue::ExternalLinkage, "external", module);
    require(verify_ir(compilation) && verify_ir(compilation), "valid synthetic IR rejected");
    require(compilation.result().status() == CompilationStatus::incomplete, "verification completed compilation");
    Compilation moved(std::move(compilation));
    require(verify_ir(moved), "verification failed after owner move");
    const auto result = std::move(moved).take_result();
    require(result.outputs().empty() && result.diagnostics().empty(), "valid verification produced output");
}

// Reject errors once and preserve diagnostics after LLVM teardown, with no staged artifact escaping.
void check_rejected(Compilation &compilation, const std::string &detail) {
    require(compilation.result().add_output({"first", OutputKind::object, {std::byte{1}}}), "fixture staging failed");
    require(!verify_ir(compilation), "invalid IR passed verification");
    require(!verify_ir(compilation), "failed batch passed repeated verification");
    require(!compilation.result().complete(), "failed verification became successful");
    require(!compilation.result().add_output({"second", OutputKind::llvm_ir, {std::byte{2}}}),
            "failed batch accepted output");
    const auto result = std::move(compilation).take_result();
    require(result.status() == CompilationStatus::failed && result.outputs().empty(), "failure retained artifacts");
    require(result.diagnostics().size() == 1, "missing or duplicate verification diagnostic");
    const auto &diagnostic = result.diagnostics().front();
    require(diagnostic.level == DiagnosticLevel::error, "verification did not report an error");
    require(diagnostic.message.contains("LLVM IR verification"), "verification diagnostic lost its phase");
    require(diagnostic.message.contains(detail), "verification diagnostic lost failure detail");
}

// Rechecking after a mutation catches a missing terminator despite earlier successful verification.
void check_function_failure() {
    auto compilation = synthetic_batch();
    require(verify_ir(compilation), "valid baseline failed");
    auto &function = *detail::state(compilation).modules.back()->getFunction("answer");
    function.getEntryBlock().getTerminator()->eraseFromParent();
    check_rejected(compilation, "module 'second.erl': function 'answer': Basic Block");
}

// LLVM can construct mismatched return types; the gate must reject them without an assertion or abort.
void check_return_failure() {
    auto compilation = synthetic_batch();
    auto &module = *detail::state(compilation).modules.back();
    auto &block = module.getFunction("answer")->getEntryBlock();
    block.getTerminator()->eraseFromParent();
    llvm::IRBuilder<> builder(&block);
    builder.CreateRet(builder.getInt64(42));
    check_rejected(compilation, "Function return type does not match operand type");
}

// A malformed global with valid function bodies proves whole-module verification is required.
void check_module_failure() {
    auto compilation = synthetic_batch();
    auto &module = *detail::state(compilation).modules.back();
    llvm::IRBuilder<> builder(module.getContext());
    module.getOrInsertGlobal("invalid_common", builder.getInt32Ty());
    auto *global = module.getNamedGlobal("invalid_common");
    global->setLinkage(llvm::GlobalValue::CommonLinkage);
    global->setInitializer(builder.getInt32(1));
    check_rejected(compilation, "'common' global must have a zero initializer");
}

// Earlier failures and completed results cannot enter another verification/emission phase.
void check_status() {
    auto failed = synthetic_batch();
    failed.result().report({DiagnosticLevel::error, "earlier failure", {}, {}});
    require(!verify_ir(failed) && failed.result().diagnostics().size() == 1, "earlier failure was not preserved");
    auto completed = synthetic_batch();
    require(verify_ir(completed) && completed.result().complete(), "valid batch could not complete");
    require(!verify_ir(completed) && completed.result().diagnostics().empty(), "completed batch reentered pipeline");
}

// Report independent verifier scenarios without aborting the host on an ordinary test failure.
int main() {
    try {
        check_valid();
        check_function_failure();
        check_return_failure();
        check_module_failure();
        check_status();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "codegen_verification: ok\n";
    return 0;
}
