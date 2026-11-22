#include "codegen/term_abi.hpp"
#include "codegen/emission.hpp"
#include "codegen/llvm_state.hpp"
#include "codegen/target.hpp"
#include <clause/abi/term.hpp>
#include <iostream>
#include <llvm/IR/IRBuilder.h>
#include <stdexcept>
#include <type_traits>

using namespace clause;
using namespace clause::codegen;
using namespace clause::abi::v1;
static_assert(std::is_same_v<decltype(std::declval<GeneratedFunction &>()(nullptr, nullptr)), TermWord>);

// Keep assertions active in release builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Retain one module with a configured target; no runtime or source lowering is involved.
Compilation target_batch(const std::string &triple) {
    CompilationRequest request;
    request.target_triple = triple;
    request.inputs.emplace_back("abi.erl", ast::Module{});
    Compilation compilation(std::move(request));
    require(configure_target(compilation), "ABI target setup failed");
    return compilation;
}

// Compare signed host decoding against LLVM's target-width two's-complement payload interpretation.
template <unsigned Bits> void check_constant(llvm::IntegerType *type) {
    using Encoding = IntegerEncoding<Bits>;
    for (const auto value : {Encoding::minimum, std::int64_t{-42}, std::int64_t{0}, Encoding::maximum}) {
        const auto encoded = Encoding::encode(value);
        auto *constant = llvm::ConstantInt::get(type, *encoded);
        require(constant->getValue().getBitWidth() == Bits, "LLVM constant used the host width");
        require(constant->getValue().ashr(4).getSExtValue() == value, "LLVM/C++ integer encoding disagrees");
    }
}

// Emit a native-convention synthetic ABI function and inspect layout against the selected target.
void check_target(const std::string &triple, unsigned bits) {
    auto compilation = target_batch(triple);
    auto *word = term_type(compilation);
    auto *signature = generated_function_type(compilation);
    require(word != nullptr && signature != nullptr, "ABI type construction failed");
    require(word->getBitWidth() == bits && signature->getReturnType() == word, "incorrect ABI word type");
    require(signature->getNumParams() == 2 && !signature->isVarArg(), "incorrect ABI arguments");
    require(signature->getParamType(0)->isPointerTy() && signature->getParamType(1)->isPointerTy(),
            "ABI context/array must be pointers");
    const auto layout = detail::state(compilation).target_machine->createDataLayout();
    require(layout.getTypeAllocSize(word) == bits / 8, "LLVM term size disagrees");
    require(layout.getABITypeAlign(word).value() == bits / 8, "LLVM term alignment disagrees");
    if (bits == 32) {
        check_constant<32>(word);
    } else {
        check_constant<64>(word);
    }
    auto &module = *detail::state(compilation).modules.front();
    auto *function = llvm::Function::Create(signature, llvm::GlobalValue::ExternalLinkage, "abi_answer", module);
    function->setCallingConv(llvm::CallingConv::C);
    llvm::IRBuilder<> builder(llvm::BasicBlock::Create(module.getContext(), "entry", function));
    builder.CreateRet(llvm::ConstantInt::get(word, 0x2af));
    require(emit_objects(compilation), "ABI signature failed verification/emission");
}

// Native width/alignment agrees with the C++ header; cross-target checks stay independent of host sizeof.
int main() {
    try {
        check_target({}, sizeof(TermWord) * 8);
#ifdef CLAUSE_LLVM_X86
        check_target("i686-unknown-linux-gnu", 32);
        check_target("i686-pc-windows-msvc", 32);
        check_target("x86_64-pc-windows-msvc", 64);
#endif
#ifdef CLAUSE_LLVM_ARM
        check_target("armv7-unknown-linux-gnueabihf", 32);
#endif
#ifdef CLAUSE_LLVM_AArch64
        check_target("aarch64-unknown-linux-gnu", 64);
#endif
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "codegen_term_abi: ok\n";
    return 0;
}
