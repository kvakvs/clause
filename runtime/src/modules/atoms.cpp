#include "atoms.hpp"
#include <algorithm>
#include <atomic>
#include <clause/abi/funs.hpp>
#include <clause/abi/records.hpp>

namespace clause::runtime {
namespace {
// Normalize the one permitted null spelling pointer without hiding invalid nonempty descriptors.
bool valid_atom(const abi::v1::AtomDescriptor &atom) {
    if (!atom.spelling && atom.size) {
        return false;
    }
    return valid_atom_spelling({atom.spelling ? atom.spelling : "", atom.size});
}

// Reject malformed descriptors without retaining any prefix of their spelling table.
bool valid_spellings(const abi::v1::ModuleDescriptor &descriptor) {
    if (!valid_atom_spelling({descriptor.name, descriptor.name_size})) {
        return false;
    }
    for (const auto &atom : std::span(descriptor.atoms, descriptor.atom_count)) {
        if (!valid_atom(atom)) {
            return false;
        }
    }
    for (const auto &item : std::span(descriptor.exports, descriptor.export_count)) {
        if (!item.name || !valid_atom_spelling({item.name, item.name_size})) {
            return false;
        }
    }
    return true;
}

// Metadata spellings share the same bounded runtime table as executable literals; the module and export names
// are bound for dynamic calls.
CodeResult<void> bind_metadata(AtomStorage &storage, const abi::v1::ModuleDescriptor &descriptor,
                               ModuleAtoms &bindings) {
    const auto module = storage.intern(std::string_view{descriptor.name, descriptor.name_size});
    if (!module) {
        return std::unexpected(CodeError::resource_limit);
    }
    bindings.module = module->word();
    bindings.exports.reserve(descriptor.export_count);
    for (const auto &item : std::span(descriptor.exports, descriptor.export_count)) {
        const auto name = storage.intern(std::string_view{item.name, item.name_size});
        if (!name) {
            return std::unexpected(CodeError::resource_limit);
        }
        bindings.exports.push_back({name->word(), item.arity, item.frame});
    }
    return {};
}

// A record names only slots of its own module, so its atoms are bound before any cell can use them.
bool valid_record(const abi::v1::ModuleDescriptor &module, const abi::v1::RecordDescriptor &record) {
    const auto slot = [&](std::size_t index) { return index < module.atom_count; };
    if (record.module != &module || !slot(record.module_atom) || !slot(record.name_atom) ||
        (record.field_count && !record.fields)) {
        return false;
    }
    return std::ranges::all_of(std::span(record.fields, record.field_count), slot);
}

// Bind every record definition to the module's atom words in descriptor order.
CodeResult<void> bind_records(ModuleAtoms &bindings, const abi::v1::ModuleDescriptor &descriptor) {
    if (descriptor.record_count && !descriptor.records) {
        return std::unexpected(CodeError::invalid_module);
    }
    const auto word = [&](std::size_t slot) { return bindings.slots[slot].word(); };
    bindings.records.reserve(descriptor.record_count);
    for (const auto &record : std::span(descriptor.records, descriptor.record_count)) {
        if (!valid_record(descriptor, record)) {
            return std::unexpected(CodeError::invalid_module);
        }
        RecordDefinition bound{&record, word(record.module_atom), word(record.name_atom), record.exported != 0, {}, {}};
        if (record.definition_size != 0) {
            bound.definition = std::string_view(record.definition, record.definition_size);
        }
        bound.fields.reserve(record.field_count);
        for (const auto slot : std::span(record.fields, record.field_count)) {
            bound.fields.push_back(word(slot));
        }
        bindings.records.push_back(std::move(bound));
    }
    return {};
}

// Bind the atom slots of the features the module was compiled with (plan 21 step LIB8B).
CodeResult<void> bind_features(ModuleAtoms &bindings, const abi::v1::ModuleDescriptor &descriptor) {
    if (descriptor.feature_count != 0 && descriptor.features == nullptr) {
        return std::unexpected(CodeError::invalid_module);
    }
    for (const auto slot : std::span(descriptor.features, descriptor.feature_count)) {
        if (slot >= descriptor.atom_count) {
            return std::unexpected(CodeError::invalid_module);
        }
        bindings.features.push_back(bindings.slots[slot].word());
    }
    return {};
}

// A fun names only slots of its own module; a local fun's code takes its arguments, then its captured values. A
// local fun without code is one only unreachable code creates (plan step 62C).
bool valid_fun(const abi::v1::ModuleDescriptor &module, const abi::v1::FunDescriptor &fun) {
    const auto slot = [&](std::size_t index) { return index < module.atom_count; };
    if (fun.module != &module || !slot(fun.module_atom) || !slot(fun.function_atom)) {
        return false;
    }
    return fun.external || !fun.frame || fun.frame->arity >= fun.arity;
}

// The code a fun enters: its descriptor's, else for an external fun nothing in the program exports the builtin of
// that name, if any (fun erlang:abs/1).
const void *fun_frame(const ModuleAtoms &bindings, const abi::v1::FunDescriptor &fun, const BuiltinRegistry &builtins) {
    if (fun.frame || !fun.external) {
        return fun.frame;
    }
    const auto module = bindings.slots[fun.module_atom].atom_spelling();
    const auto function = bindings.slots[fun.function_atom].atom_spelling();
    return module && function ? builtins.find(*module, *function, fun.arity) : nullptr;
}

// Bind every fun definition to the module's atom words in descriptor order; only an external fun may lack code.
CodeResult<void> bind_funs(ModuleAtoms &bindings, const abi::v1::ModuleDescriptor &descriptor,
                           const BuiltinRegistry &builtins) {
    if (descriptor.fun_count && !descriptor.funs) {
        return std::unexpected(CodeError::invalid_module);
    }
    const auto word = [&](std::size_t slot) { return bindings.slots[slot].word(); };
    bindings.funs.reserve(descriptor.fun_count);
    for (const auto &fun : std::span(descriptor.funs, descriptor.fun_count)) {
        if (!valid_fun(descriptor, fun)) {
            return std::unexpected(CodeError::invalid_module);
        }
        const std::size_t captures = fun.external || !fun.frame ? 0 : fun.frame->arity - fun.arity;
        bindings.funs.push_back(FunDefinition{.descriptor = &fun,
                                              .module = word(fun.module_atom),
                                              .function = word(fun.function_atom),
                                              .arity = fun.arity,
                                              .index = fun.index,
                                              .external = fun.external != 0,
                                              .captures = captures,
                                              .frame = fun_frame(bindings, fun, builtins)});
    }
    return {};
}
} // namespace

namespace {
// Claim the descriptor's atom cache for this runtime and fill it with the slot words, and its fun cache with the fun
// definitions (inline closures); a cache another runtime claimed stays its (OPT0.8).
void claim_cache(ModuleAtoms &bindings, const abi::v1::ModuleDescriptor &descriptor, const std::uint64_t runtime) {
    if (!descriptor.atom_cache || runtime == 0) {
        return;
    }
    std::atomic_ref claim(descriptor.atom_cache[0]);
    Word unclaimed = 0;
    if (!claim.compare_exchange_strong(unclaimed, static_cast<Word>(runtime))) {
        return;
    }
    for (std::size_t slot = 0; slot < bindings.slots.size(); ++slot) {
        descriptor.atom_cache[slot + 1] = bindings.slots[slot].word();
    }
    if (descriptor.fun_cache) {
        for (std::size_t fun = 0; fun < bindings.funs.size(); ++fun) {
            descriptor.fun_cache[fun] = reinterpret_cast<Word>(&bindings.funs[fun]);
        }
    }
    bindings.claimed_cache = descriptor.atom_cache;
}
} // namespace

ModuleAtoms::~ModuleAtoms() {
    if (claimed_cache) {
        std::atomic_ref(claimed_cache[0]).store(0);
    }
}

CodeResult<std::shared_ptr<ModuleAtoms>> bind_atoms(AtomStorage &storage, const abi::v1::ModuleDescriptor &descriptor,
                                                    const BuiltinRegistry &builtins, const std::uint64_t runtime) {
    if (!valid_spellings(descriptor)) {
        return std::unexpected(CodeError::invalid_module);
    }
    auto bindings = std::make_shared<ModuleAtoms>();
    const auto metadata = bind_metadata(storage, descriptor, *bindings);
    if (!metadata) {
        return std::unexpected(metadata.error());
    }
    bindings->descriptor = &descriptor;
    bindings->slots.reserve(descriptor.atom_count);
    for (const auto &atom : std::span(descriptor.atoms, descriptor.atom_count)) {
        const auto term = storage.intern(std::string_view{atom.spelling ? atom.spelling : "", atom.size});
        if (!term) {
            return std::unexpected(CodeError::resource_limit);
        }
        bindings->slots.push_back(*term);
    }
    if (const auto records = bind_records(*bindings, descriptor); !records) {
        return std::unexpected(records.error());
    }
    if (const auto funs = bind_funs(*bindings, descriptor, builtins); !funs) {
        return std::unexpected(funs.error());
    }
    if (const auto features = bind_features(*bindings, descriptor); !features) {
        return std::unexpected(features.error());
    }
    claim_cache(*bindings, descriptor, runtime);
    return bindings;
}
} // namespace clause::runtime

clause::abi::v1::TermWord CLAUSE_atom_v3(void *context, std::size_t slot, const void *descriptor) noexcept {
    using namespace clause;
    if (!context) {
        return 0;
    }
    auto &owner = *static_cast<runtime::ProcessContext *>(context);
    if (!owner.generated_calls().active() || owner.generated_calls().failure()) {
        return 0;
    }
    const auto word = owner.code_server().atom_word(descriptor, slot);
    if (!word) {
        owner.generated_calls().fail_service(abi::v1::Status::wrong_owner);
        return 0;
    }
    return *word;
}
