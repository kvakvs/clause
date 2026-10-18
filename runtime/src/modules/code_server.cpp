#include <algorithm>
#include <clause/runtime/code_server.hpp>
#include <mutex>

namespace clause::runtime {
std::shared_ptr<const CodeImage> CodeImage::linked() { return std::make_shared<CodeImage>(); }

// Copy spelling inside the caller's failure boundary; Debug STL string moves may allocate in noexcept code.
LoadedModule::LoadedModule(ModuleDefinition &&definition)
    : definition_{definition.name, definition.image, std::move(definition.functions), definition.atoms} {}

std::string_view LoadedModule::name() const noexcept { return definition_.name; }

const ModuleRegistry &LoadedModule::functions() const noexcept { return *definition_.functions; }

const ModuleAtoms *LoadedModule::atoms() const noexcept { return definition_.atoms.get(); }

std::size_t FunctionAtomsHash::operator()(const FunctionAtoms &name) const noexcept {
    // Boost-style combination of the three word hashes.
    auto seed = std::hash<Word>{}(name.module);
    for (const auto part : {std::hash<Word>{}(name.function), std::hash<std::size_t>{}(name.arity)}) {
        seed ^= part + 0x9e3779b9U + (seed << 6U) + (seed >> 2U);
    }
    return seed;
}

namespace {
// The value `map` holds for `key`, or null.
template <typename Map> auto found(const Map &map, const typename Map::key_type &key) noexcept {
    const auto entry = map.find(key);
    return entry == map.end() ? nullptr : entry->second;
}

// The spellings of the atom words a module's slots pin, by word. Added for step 41B.
std::unordered_map<Word, std::string_view> slot_spellings(const ModuleAtoms &atoms) {
    std::unordered_map<Word, std::string_view> result;
    for (const auto &slot : atoms.slots) {
        if (const auto spelling = slot.atom_spelling()) {
            result.emplace(slot.word(), *spelling);
        }
    }
    return result;
}
} // namespace

bool CodeServer::builtin_replaces(const std::unordered_map<Word, std::string_view> &spellings, const Word module,
                                  const ExportFrame &item) const noexcept {
    const auto module_name = spellings.find(module);
    const auto function_name = spellings.find(item.function);
    return module_name != spellings.end() && function_name != spellings.end() &&
           builtins_.find(module_name->second, function_name->second, item.arity) != nullptr;
}

const ModuleAtoms *CodeServer::find_atoms(const void *descriptor) const noexcept {
    return found(modules_by_descriptor_, descriptor);
}

void CodeServer::index(const ModuleAtoms &atoms) {
    try {
        modules_by_descriptor_.emplace(atoms.descriptor, &atoms);
        for (const auto &record : atoms.records) {
            records_.emplace(record.descriptor, &record);
        }
        for (const auto &fun : atoms.funs) {
            funs_.emplace(fun.descriptor, &fun);
        }
        const auto spellings = slot_spellings(atoms);
        for (const auto &item : atoms.exports) {
            if (!builtin_replaces(spellings, atoms.module, item)) {
                exports_.emplace(FunctionAtoms{atoms.module, item.function, item.arity}, item.frame);
            }
        }
    } catch (...) {
        unindex(atoms);
        throw;
    }
}

void CodeServer::unindex(const ModuleAtoms &atoms) noexcept {
    // Erase only entries of this module: a key another module holds is left alone.
    const auto erase = [](auto &map, const auto &key, const auto *value) {
        if (const auto entry = map.find(key); entry != map.end() && entry->second == value) {
            map.erase(entry);
        }
    };
    erase(modules_by_descriptor_, atoms.descriptor, &atoms);
    for (const auto &record : atoms.records) {
        erase(records_, record.descriptor, &record);
    }
    for (const auto &fun : atoms.funs) {
        erase(funs_, fun.descriptor, &fun);
    }
    for (const auto &item : atoms.exports) {
        erase(exports_, FunctionAtoms{atoms.module, item.function, item.arity}, item.frame);
    }
}

std::vector<std::string> CodeServer::module_names() const {
    const std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    const auto builtin_modules = builtins_.modules();
    names.reserve(modules_.size() + builtin_modules.size());
    for (const auto &[name, module] : modules_) {
        names.push_back(name);
    }
    for (const auto module : builtin_modules) {
        if (!modules_.contains(module)) {
            names.emplace_back(module);
        }
    }
    std::ranges::sort(names);
    return names;
}

TermResult<Word> CodeServer::atom_word(const void *descriptor, std::size_t slot) const noexcept {
    const std::shared_lock lock(mutex_);
    const auto *atoms = find_atoms(descriptor);
    if (!atoms) {
        return std::unexpected(TermError::wrong_owner);
    }
    if (slot >= atoms->slots.size()) {
        return std::unexpected(TermError::out_of_range);
    }
    return atoms->slots[slot].word();
}

const RecordDefinition *CodeServer::record_definition(const void *descriptor) const noexcept {
    const std::shared_lock lock(mutex_);
    return found(records_, descriptor);
}

const FunDefinition *CodeServer::fun_definition(const void *descriptor) const noexcept {
    const std::shared_lock lock(mutex_);
    return find_fun(descriptor);
}

const FunDefinition *CodeServer::find_fun(const void *descriptor) const noexcept { return found(funs_, descriptor); }

const void *CodeServer::export_frame(const FunctionAtoms &name) const noexcept {
    const std::shared_lock lock(mutex_);
    return find_export(name);
}

const void *CodeServer::find_export(const FunctionAtoms &name) const noexcept { return found(exports_, name); }

const void *CodeServer::function_frame(const Term &module, const Term &function, std::size_t arity) const noexcept {
    const std::shared_lock lock(mutex_);
    return find_function(module, function, arity);
}

const void *CodeServer::find_function(const Term &module, const Term &function, std::size_t arity) const noexcept {
    if (const auto *frame = find_export({module.word(), function.word(), arity})) {
        return frame;
    }
    const auto module_name = module.atom_spelling();
    const auto function_name = function.atom_spelling();
    return module_name && function_name ? builtins_.find(*module_name, *function_name, arity) : nullptr;
}

bool CodeServer::exported(const Term &module, const Term &function, std::size_t arity) const noexcept {
    const std::shared_lock lock(mutex_);
    return exports_.contains({module.word(), function.word(), arity}) || find_function(module, function, arity);
}

const FunDefinition &CodeServer::external_fun(const Term &module, const Term &function, std::size_t arity) {
    const FunctionAtoms key{module.word(), function.word(), arity};
    {
        const std::shared_lock lock(mutex_);
        if (const auto found = external_funs_.find(key); found != external_funs_.end()) {
            return *found->second;
        }
    }
    const std::unique_lock lock(mutex_);
    // Another worker may have built the definition between the two locks.
    auto &definition = external_funs_[key];
    if (!definition) {
        definition = std::make_unique<FunDefinition>(FunDefinition{.module = key.module,
                                                                   .function = key.function,
                                                                   .arity = arity,
                                                                   .external = true,
                                                                   .frame = find_function(module, function, arity)});
    }
    return *definition;
}

const RecordDefinition &CodeServer::created_record(const CreatedRecordKey &key) {
    {
        const std::shared_lock lock(mutex_);
        if (const auto found = created_records_.find(key); found != created_records_.end()) {
            return *found->second;
        }
    }
    const std::unique_lock lock(mutex_);
    // Another worker may have built the definition between the two locks.
    auto &definition = created_records_[key];
    if (!definition) {
        definition = std::make_unique<RecordDefinition>();
        definition->module = key.module;
        definition->name = key.name;
        definition->exported = key.exported;
        definition->fields = key.fields;
        definition->descriptor = definition.get();
        records_.emplace(definition->descriptor, definition.get());
    }
    return *definition;
}

const ModuleAtoms *CodeServer::find_module_atoms(Word module) const noexcept {
    for (const auto &[name, loaded] : modules_) {
        const auto *atoms = loaded->atoms();
        if (atoms != nullptr && atoms->module == module) {
            return atoms;
        }
    }
    return nullptr;
}

const RecordDefinition *CodeServer::module_record(const RecordName &name) const noexcept {
    const std::shared_lock lock(mutex_);
    const auto *atoms = find_module_atoms(name.module);
    if (atoms == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find(atoms->records, name.name, &RecordDefinition::name);
    return found == atoms->records.end() ? nullptr : &*found;
}

std::optional<std::vector<Word>> CodeServer::module_features(Word module) const {
    const std::shared_lock lock(mutex_);
    const auto *atoms = find_module_atoms(module);
    return atoms == nullptr ? std::nullopt : std::optional{atoms->features};
}

bool CodeServer::owns(const FunDefinition &definition) const noexcept {
    const std::shared_lock lock(mutex_);
    if (definition.descriptor) {
        return find_fun(definition.descriptor) == &definition;
    }
    const auto found = external_funs_.find({definition.module, definition.function, definition.arity});
    return found != external_funs_.end() && found->second.get() == &definition;
}

void CodeServer::publish(const std::shared_ptr<const LoadedModule> &module) {
    const auto *atoms = module->atoms();
    if (!atoms) {
        modules_.emplace(module->name(), module);
        return;
    }
    index(*atoms);
    try {
        modules_.emplace(module->name(), module);
    } catch (...) {
        unindex(*atoms);
        throw;
    }
}

CodeResult<std::shared_ptr<const LoadedModule>> CodeServer::load(ModuleDefinition &&definition) {
    if (definition.name.empty() || !definition.image || !definition.functions) {
        return std::unexpected(CodeError::invalid_module);
    }
    const std::unique_lock lock(mutex_);
    if (modules_.contains(definition.name)) {
        return std::unexpected(CodeError::duplicate_module);
    }
    if (definition.atoms && find_atoms(definition.atoms->descriptor)) {
        return std::unexpected(CodeError::invalid_module);
    }
    try {
        auto module = std::shared_ptr<LoadedModule>(new LoadedModule(std::move(definition)));
        module->definition_.functions->freeze();
        publish(module);
        return module;
    } catch (const std::bad_alloc &) {
        return std::unexpected(CodeError::resource_limit);
    }
}

CodeResult<std::shared_ptr<const LoadedModule>> CodeServer::find_module(std::string_view name) const {
    const std::shared_lock lock(mutex_);
    const auto found = modules_.find(name);
    if (found == modules_.end()) {
        return std::unexpected(CodeError::module_not_found);
    }
    return found->second;
}

CodeResult<ResolvedFunction> CodeServer::resolve(FunctionRequest request) const {
    const auto found = find_module(request.module);
    if (!found) {
        return std::unexpected(found.error());
    }
    const auto target = (*found)->functions().find(request.function, request.arity);
    if (!target) {
        if (target.error() == RegistryError::resource_limit) {
            return std::unexpected(CodeError::resource_limit);
        }
        return std::unexpected(CodeError::function_not_exported);
    }
    return ResolvedFunction(*found, *target, request.arity);
}
} // namespace clause::runtime
