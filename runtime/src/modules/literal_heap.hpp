#pragma once
#include <clause/abi/modules.hpp>
#include <clause/runtime/code_server.hpp>
#include <clause/runtime/runtime.hpp>

namespace clause::runtime {
// Decode a module's literals into the runtime's literal heap (literal heap): their words go to `bindings.literals`
// and the area holding them to `bindings.literal_heap`; when this runtime claimed the module's caches, the
// descriptor's literal cache gets the words too.
CodeResult<void> decode_literals(Runtime &runtime, const abi::v1::ModuleDescriptor &descriptor, ModuleAtoms &bindings);
} // namespace clause::runtime
