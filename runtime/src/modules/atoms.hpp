#pragma once
#include <clause/runtime/modules.hpp>

namespace clause::runtime {
// Validate every spelling before interning, then stage immutable bindings for atomic module publication. External
// funs nothing in the program exports bind to the builtin of their name, if any.
CodeResult<std::shared_ptr<const ModuleAtoms>> bind_atoms(AtomStorage &storage,
                                                          const abi::v1::ModuleDescriptor &descriptor,
                                                          const BuiltinRegistry &builtins, std::uint64_t runtime);
} // namespace clause::runtime
