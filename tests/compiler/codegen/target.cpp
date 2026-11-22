#include "codegen/target.hpp"
#include "codegen/llvm_state.hpp"
#include <bit>
#include <climits>
#include <iostream>
#include <llvm/TargetParser/Host.h>
#include <stdexcept>
#include <string>
#include <utility>

using namespace clause;
using namespace clause::codegen;

// Keep target-contract checks active in release builds as well as debug builds.
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Create two empty modules so every target setup exercises whole-batch propagation.
CompilationRequest request(std::string triple = {}) {
    CompilationRequest batch;
    batch.target_triple = std::move(triple);
    batch.inputs.emplace_back("first.erl", ast::Module{});
    batch.inputs.emplace_back("second.erl", ast::Module{});
    return batch;
}

// Check the target-derived layout, relocation contract and unchanged incomplete result.
void check_layout(const Compilation &compilation, unsigned bits) {
    const auto &state = detail::state(compilation);
    const auto &machine = *state.target_machine;
    require(machine.createDataLayout().getPointerSizeInBits() == bits, "wrong target pointer width");
    require(machine.getRelocationModel() == llvm::Reloc::PIC_, "unexpected relocation model");
    require(machine.getCodeModel() == llvm::CodeModel::Small, "unexpected code model");
    for (const auto &module : state.modules) {
        require(module->getTargetTriple() == machine.getTargetTriple(), "module target mismatch");
        require(module->getDataLayout() == machine.createDataLayout(), "module layout mismatch");
        require(module->empty(), "target setup unexpectedly lowered code");
    }
    require(compilation.result().status() == CompilationStatus::incomplete, "target setup completed compilation");
    require(compilation.result().diagnostics().empty() && compilation.result().outputs().empty(), "unexpected results");
}

// Native selection follows the running process, retains detected capabilities and survives moves.
void check_native() {
    Compilation compilation(request());
    require(configure_target(compilation), "native target setup failed");
    check_layout(compilation, sizeof(void *) * CHAR_BIT);
    const auto *machine = detail::state(compilation).target_machine.get();
    const auto layout = machine->createDataLayout();
    require(layout.getPointerABIAlignment(0).value() == alignof(void *), "native pointer alignment mismatch");
    require(layout.isLittleEndian() == (std::endian::native == std::endian::little), "native byte order mismatch");
    const llvm::Triple host(llvm::Triple::normalize(llvm::sys::getProcessTriple()));
    require(machine->getTargetTriple() == host, "default target is not the process host");
    require(machine->getTargetCPU() == llvm::sys::getHostCPUName(), "host CPU preference lost");
    require(machine->getOptLevel() == llvm::CodeGenOptLevel::None, "default machine optimization is not O0");
    const auto features = "," + machine->getTargetFeatureString().str() + ",";
    for (const auto &feature : llvm::sys::getHostCPUFeatures()) {
        const auto spelling = std::string(feature.second ? ",+" : ",-") + feature.first().str() + ",";
        require(features.contains(spelling), "detected host feature lost");
    }
    Compilation moved(std::move(compilation));
    require(configure_target(moved) && detail::state(moved).target_machine.get() == machine,
            "machine rebuilt after move");
    Compilation explicit_host(request(host.str()));
    require(configure_target(explicit_host), "explicit host setup failed");
    require(detail::state(explicit_host).target_machine->getTargetFeatureString() == machine->getTargetFeatureString(),
            "explicit host differs from native default");
    const auto result = std::move(moved).take_result();
    require(result.status() == CompilationStatus::incomplete, "target teardown changed status");
}

// Foreign targets retain their requested architecture/OS, baseline CPU and target-sized words.
void check_foreign(const std::string &triple, unsigned bits, llvm::Triple::ObjectFormatType format) {
    auto batch = request(triple);
    batch.optimization = OptimizationLevel::speed;
    Compilation compilation(std::move(batch));
    require(configure_target(compilation), "foreign target setup failed");
    check_layout(compilation, bits);
    const auto &machine = *detail::state(compilation).target_machine;
    require(machine.getTargetTriple() == llvm::Triple(llvm::Triple::normalize(triple)), "requested triple lost");
    const llvm::Triple host(llvm::Triple::normalize(llvm::sys::getProcessTriple()));
    if (machine.getTargetTriple() != host) {
        require(machine.getTargetCPU() == "generic" && machine.getTargetFeatureString().empty(),
                "host leaked into target");
    }
    require(machine.getTargetTriple().getObjectFormat() == format, "wrong target object format");
    require(machine.getOptLevel() == llvm::CodeGenOptLevel::Default, "speed policy lost");
}

// Exercise each configured backend without claiming foreign object emission or execution. Unknown and unavailable
// targets are rejected through the CLI by codegen_cross_targets.
void check_cross_targets() {
#ifdef CLAUSE_LLVM_X86
    check_foreign("i686-unknown-linux-gnu", 32, llvm::Triple::ELF);
    check_foreign("x86_64-unknown-linux-gnu", 64, llvm::Triple::ELF);
    check_foreign("x86_64-linux-gnu", 64, llvm::Triple::ELF);
    check_foreign("i686-pc-windows-msvc", 32, llvm::Triple::COFF);
    check_foreign("x86_64-pc-windows-msvc", 64, llvm::Triple::COFF);
#endif
#ifdef CLAUSE_LLVM_ARM
    check_foreign("armv7-unknown-linux-gnueabihf", 32, llvm::Triple::ELF);
#endif
#ifdef CLAUSE_LLVM_AArch64
    check_foreign("aarch64-unknown-linux-gnu", 64, llvm::Triple::ELF);
#endif
}

// Report independent target scenarios without aborting on ordinary test failures.
int main() {
    try {
        check_native();
        check_cross_targets();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "codegen_target: ok\n";
    return 0;
}
