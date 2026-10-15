#include "atoms.hpp"
#include <array>
#include <clause/runtime/modules.hpp>

namespace clause::runtime {
namespace {
// Validate the fixed prefix before inspecting target-width-dependent descriptor fields.
CodeResult<void> validate(const abi::v1::ModuleDescriptor &descriptor) {
    if (descriptor.abi_version != abi::v1::version || descriptor.term_bits != sizeof(Word) * 8) {
        return std::unexpected(CodeError::abi_mismatch);
    }
    if (!descriptor.name || descriptor.name_size == 0 || (descriptor.export_count && !descriptor.exports)) {
        return std::unexpected(CodeError::invalid_module);
    }
    if ((descriptor.atom_count && !descriptor.atoms) || descriptor.atom_count > AtomStorage::hard_limit) {
        return std::unexpected(CodeError::invalid_module);
    }
    return {};
}

// Contain native entry exceptions while the invocation channel still owns its first failure.
CallResult<Word> invoke_entry(abi::v1::GeneratedFunction *entry, ProcessContext &context,
                              const Word *arguments) noexcept {
    try {
        return entry(&context, arguments);
    } catch (const std::bad_alloc &) {
        return std::unexpected(CallFailure{CallError::resource_limit});
    } catch (...) {
        return std::unexpected(CallFailure{CallError::native_exception});
    }
}

// Marshal checked host Terms without aliasing their C++ object representation.
CallResult<Term> invoke(abi::v1::GeneratedFunction *entry, ProcessContext &context, std::span<const Term> arguments) {
    auto &state = context.generated_calls();
    GeneratedInvocation invocation(state);
    if (state.failure()) {
        return std::unexpected(*state.failure());
    }
    std::array<Word, 255> words{};
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        words[i] = arguments[i].word();
    }
    const auto word = invoke_entry(entry, context, arguments.empty() ? nullptr : words.data());
    if (state.failure()) {
        return std::unexpected(*state.failure());
    }
    if (!word) {
        return std::unexpected(word.error());
    }
    const auto result = Term::from_word(*word, context);
    if (!result) {
        return std::unexpected(CallFailure{CallError::argument_type_mismatch, {}, result.error()});
    }
    return *result;
}

// A named export with a valid arity; one without entry and frame keeps only its name: the executable never calls
// it (plan step 62C).
bool valid_export(const abi::v1::ExportDescriptor &item) {
    return item.name && item.name_size != 0 && (item.entry || !item.frame) && item.arity <= 255;
}

// Add one export's generic host target.
CodeResult<void> add_export(ModuleRegistry &registry, const abi::v1::ExportDescriptor &item) {
    auto added = registry.add({item.name, item.name_size}, item.arity,
                              [entry = item.entry](ProcessContext &context, std::span<const Term> arguments) {
                                  return invoke(entry, context, arguments);
                              });
    if (!added) {
        return std::unexpected(added.error() == RegistryError::resource_limit ? CodeError::resource_limit
                                                                              : CodeError::invalid_export);
    }
    return {};
}

// Build all generic targets privately so any malformed or duplicate export rolls back the draft.
CodeResult<void> add_exports(ModuleRegistry &registry, const abi::v1::ModuleDescriptor &descriptor) {
    for (const auto &item : std::span(descriptor.exports, descriptor.export_count)) {
        if (!valid_export(item)) {
            return std::unexpected(CodeError::invalid_export);
        }
        if (!item.entry) {
            continue;
        }
        if (auto added = add_export(registry, item); !added) {
            return added;
        }
    }
    return {};
}
} // namespace

CodeResult<std::shared_ptr<const LoadedModule>>
register_module(Runtime &runtime, const abi::v1::ModuleDescriptor &descriptor, std::shared_ptr<const CodeImage> image) {
    const auto valid = validate(descriptor);
    if (!valid) {
        return std::unexpected(valid.error());
    }
    auto *server = runtime.code_server();
    if (!server) {
        return std::unexpected(CodeError::stopped);
    }
    if (server->find_module({descriptor.name, descriptor.name_size})) {
        return std::unexpected(CodeError::duplicate_module);
    }
    try {
        ModuleDefinition definition{
            {descriptor.name, descriptor.name_size}, std::move(image), std::make_unique<ModuleRegistry>()};
        const auto added = add_exports(*definition.functions, descriptor);
        if (!added) {
            return std::unexpected(added.error());
        }
        const auto atoms = bind_atoms(*runtime.atom_storage(), descriptor, server->builtins(), runtime.identity());
        if (!atoms) {
            return std::unexpected(atoms.error());
        }
        definition.atoms = *atoms;
        return server->load(std::move(definition));
    } catch (const std::bad_alloc &) {
        return std::unexpected(CodeError::resource_limit);
    }
}
} // namespace clause::runtime

std::uint8_t CLAUSE_register_module_v4(void *runtime, const void *descriptor) noexcept {
    using namespace clause;
    using abi::v1::Status;
    if (!runtime || !descriptor) {
        return static_cast<std::uint8_t>(Status::invalid_argument);
    }
    try {
        const auto result = runtime::register_module(*static_cast<runtime::Runtime *>(runtime),
                                                     *static_cast<const abi::v1::ModuleDescriptor *>(descriptor));
        if (result) {
            return static_cast<std::uint8_t>(Status::ok);
        }
        switch (result.error()) {
        case runtime::CodeError::abi_mismatch:
            return static_cast<std::uint8_t>(Status::abi_mismatch);
        case runtime::CodeError::stopped:
            return static_cast<std::uint8_t>(Status::stopped);
        case runtime::CodeError::resource_limit:
            return static_cast<std::uint8_t>(Status::out_of_memory);
        default:
            return static_cast<std::uint8_t>(Status::invalid_argument);
        }
    } catch (...) {
        return static_cast<std::uint8_t>(Status::internal_error);
    }
}
