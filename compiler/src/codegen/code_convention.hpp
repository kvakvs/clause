#pragma once
#include <llvm/IR/CallingConv.h>
#include <llvm/TargetParser/Triple.h>

namespace clause::codegen {
// The calling convention of Code bodies for a target (OPT0.1A follow-up): preserve_none on x86-64 and AArch64 when
// the runtime is built with Clang, as clau is; C otherwise. It must equal abi::v1::Code's in the runtime.
llvm::CallingConv::ID code_convention(const llvm::Triple &target);
} // namespace clause::codegen
