#pragma once

namespace llvm {
class Module;
class TargetMachine;
} // namespace llvm

namespace clause::codegen {
class Compilation;
class CompilationResult;

// Mandatory gate before each emission: check current target settings, function bodies and whole modules.
// Never cache success across IR mutations; failure diagnoses and invalidates all staged batch output.
bool verify_ir(Compilation &compilation);
// Verify one module like verify_ir, against `machine`, reporting into `result`. Added for parallel code generation.
bool verify_module_ir(const llvm::Module &module, const llvm::TargetMachine &machine, CompilationResult &result);
} // namespace clause::codegen
