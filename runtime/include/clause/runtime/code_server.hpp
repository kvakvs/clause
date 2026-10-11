#pragma once
#include "builtin_registry.hpp"
#include "callable.hpp"
#include "features.hpp"
#include <compare>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

namespace clause::runtime {
enum class CodeError : std::uint8_t {
    invalid_module,
    invalid_export,
    duplicate_module,
    module_not_found,
    function_not_exported,
    resource_limit,
    not_implemented,
    diagnostic_failure,
    abi_mismatch,
    stopped
};
template <typename Value> using CodeResult = std::expected<Value, CodeError>;

// Retain linked executable storage until all targets and captures have been destroyed.
class CodeImage {
  public:
    // Create a lifetime anchor for code already linked into the host program.
    static std::shared_ptr<const CodeImage> linked();
    // Future loader subclasses release their executable storage here.
    virtual ~CodeImage() = default;
};

// A native record definition bound to runtime atoms; native record cells point at it (docs/native-records.md).
struct RecordDefinition final {
    // The compiler's RecordDescriptor this was built from; a lookup key, never dereferenced at use sites.
    const void *descriptor = nullptr;
    // Atom words of the defining module and the record name.
    Word module = 0;
    Word name = 0;
    // Whether the defining module exported the record when it was compiled.
    bool exported = false;
    // Field-name atom words in definition order.
    std::vector<Word> fields;
};

// A function value's identity bound to runtime atoms; fun cells point at it (docs/funs.md).
struct FunDefinition final {
    // The compiler's FunDescriptor this was built from; a lookup key, never dereferenced at use sites.
    const void *descriptor = nullptr;
    // Atom words of the module and function a call enters: this module and its function for a local fun, M and F
    // for an external fun.
    Word module = 0;
    Word function = 0;
    // The fun's Erlang arity and a local fun's index among its module's funs.
    std::size_t arity = 0;
    std::size_t index = 0;
    bool external = false;
    // Captured values a local fun's cells hold, passed after its arguments; 0 for an external fun.
    std::size_t captures = 0;
    // The FrameDescriptor a call enters; null for an external fun the program does not export.
    const void *frame = nullptr;
};

// A function named by runtime atom words of its module and name, and its arity; the key of dynamic calls.
struct FunctionAtoms final {
    Word module = 0;
    Word function = 0;
    std::size_t arity = 0;
    auto operator<=>(const FunctionAtoms &) const = default;
};

// Hash of a FunctionAtoms key for the export index.
struct FunctionAtomsHash final {
    std::size_t operator()(const FunctionAtoms &name) const noexcept;
};

// One exported function a dynamic call can enter, bound to runtime atoms.
struct ExportFrame final {
    // Atom word of the function name and its arity.
    Word function = 0;
    std::size_t arity = 0;
    // The FrameDescriptor a call enters; null for a host-only export.
    const void *frame = nullptr;
};

struct ModuleAtoms final {
    // Explicit empty construction keeps Debug STL bookkeeping failures inside registration's catch boundary.
    ModuleAtoms() : slots(0) {}

    // Use the image-owned descriptor address solely as a stable lookup key, never dereference it at use sites.
    const void *descriptor = nullptr;
    // Pin every initialized spelling in compiler slot order, including duplicate slots.
    std::vector<Term> slots;
    // The module's native record definitions in descriptor order; never resized after registration.
    std::vector<RecordDefinition> records;
    // The module's fun definitions in descriptor order; never resized after registration.
    std::vector<FunDefinition> funs;
    // Atom word of the module name and its exports, which dynamic calls look up.
    Word module = 0;
    std::vector<ExportFrame> exports;
};

struct ModuleDefinition final {
    // Own exact module spelling independently of the optional generated atom bindings.
    std::string name;
    // Declaration order keeps executable memory alive through target destruction.
    std::shared_ptr<const CodeImage> image;
    // Transfer the sole mutable registry into its published module.
    std::unique_ptr<ModuleRegistry> functions;
    // Retain immutable runtime-specific bindings with the image and exported call handles.
    std::shared_ptr<const ModuleAtoms> atoms = {};
};

// Pin one immutable registry and its code image independently of future lookup access.
class LoadedModule final {
  public:
    // Release targets before their executable image, including callable capture destructors.
    ~LoadedModule() = default;
    LoadedModule(const LoadedModule &) = delete;
    LoadedModule &operator=(const LoadedModule &) = delete;
    // Borrow the module spelling while retaining this handle.
    std::string_view name() const noexcept;
    // Borrow the single frozen registry; direct calls require this module handle to remain live.
    const ModuleRegistry &functions() const noexcept;
    // Inspect initialized bindings while this loaded-module handle pins their code image.
    const ModuleAtoms *atoms() const noexcept;

  private:
    friend class CodeServer;
    // Adopt a validated draft without copying targets or their mutable capture state.
    explicit LoadedModule(ModuleDefinition &&definition);
    // Preserve image-before-registry declaration order for safe destruction.
    ModuleDefinition definition_;
};

class ResolvedFunction final {
  public:
    // Report the fixed arity selected by generic lookup.
    std::size_t arity() const noexcept;
    // Check immediate arguments/result and contain host exceptions; unavailable bodies report once.
    CallResult<Term> call(ProcessContext &context, std::span<const Term> arguments,
                          DiagnosticSink sink = {}) const noexcept;

  private:
    friend class CodeServer;
    // Pin the registry and image before borrowing a stable target address from them.
    ResolvedFunction(std::shared_ptr<const LoadedModule> module, const Callable *target, std::size_t arity);
    // Retain immutable code and targets even after their runtime is destroyed.
    std::shared_ptr<const LoadedModule> module_;
    // Borrow exactly one stored generic target without copying captured state.
    const Callable *target_;
    // Check span length before any target invocation.
    std::size_t arity_;
};

struct FunctionRequest final {
    // Borrow exact module/function names for one synchronous lookup.
    std::string_view module;
    std::string_view function;
    // Select the fixed generic signature without interpreting argument values.
    std::size_t arity;
};

// Runtime-owned publication service shared by scheduler workers (docs/runtime.md#threads): lookups run concurrently
// under a shared lock, publication takes it exclusively. Modules are never removed before the server is destroyed,
// after every context, so definitions and frames it returns stay valid while any process can use them.
class CodeServer final {
  public:
    // Create an empty registry; native registration never loads files or performs hot upgrades.
    CodeServer() = default;
    CodeServer(const CodeServer &) = delete;
    CodeServer &operator=(const CodeServer &) = delete;
    ~CodeServer() = default;
    // Publish a complete registry transactionally; duplicates never replace existing code.
    CodeResult<std::shared_ptr<const LoadedModule>> load(ModuleDefinition &&definition);
    // Reserve removal/hot-unload while retaining all published modules and pinned handles on failure.
    CodeResult<void> unload(std::string_view name, DiagnosticSink sink = {}) noexcept;
    // Select only the exact all-Term entry and return a module-pinning call handle.
    CodeResult<ResolvedFunction> resolve(FunctionRequest request) const;
    // Retain immutable module ownership or report a missing module.
    CodeResult<std::shared_ptr<const LoadedModule>> find_module(std::string_view name) const;
    // Resolve an initialized atom slot without allocation or spelling interning.
    TermResult<Word> atom_word(const void *descriptor, std::size_t slot) const noexcept;
    // Find the bound definition of a registered module's record descriptor; null when none is registered.
    const RecordDefinition *record_definition(const void *descriptor) const noexcept;
    // Find the bound definition of a registered module's fun descriptor; null when none is registered.
    const FunDefinition *fun_definition(const void *descriptor) const noexcept;
    // The FrameDescriptor of Module:Function/Arity exported by a registered module; null when none exports it.
    const void *export_frame(const FunctionAtoms &name) const noexcept;
    // The FrameDescriptor a call of the atoms Module:Function/Arity enters: a registered module's export, else a
    // builtin's frame; null when neither exists.
    const void *function_frame(const Term &module, const Term &function, std::size_t arity) const noexcept;
    // The definition of external fun Module:Function/Arity built at run time (fun M:F/A with variables) from atoms,
    // created on first use and kept for the server's lifetime; may throw std::bad_alloc.
    const FunDefinition &external_fun(const Term &module, const Term &function, std::size_t arity);
    // Whether `definition` is a registered module's fun definition or an external fun this server built.
    bool owns(const FunDefinition &definition) const noexcept;

    // The production builtins calls reach besides the modules' exports; registered at runtime startup, before any
    // worker runs, and read-only afterwards.
    BuiltinRegistry &builtins() noexcept { return builtins_; }

    const BuiltinRegistry &builtins() const noexcept { return builtins_; }

  private:
    // The lookups below run with the lock already held.
    // Find the immutable descriptor key without dereferencing image-owned storage.
    const ModuleAtoms *find_atoms(const void *descriptor) const noexcept;
    // Find a registered module's fun definition by descriptor.
    const FunDefinition *find_fun(const void *descriptor) const noexcept;
    // Find an export's frame by module, function atom and arity.
    const void *find_export(const FunctionAtoms &name) const noexcept;
    // Find an export's frame, else a builtin's.
    const void *find_function(const Term &module, const Term &function, std::size_t arity) const noexcept;
    // Whether a registered builtin of the same name replaces an export (a library stub such as erlang:abs/1), so
    // dynamic calls keep reaching the builtin; `spellings` names the module's atom words. Added for step 41B.
    bool builtin_replaces(const std::unordered_map<Word, std::string_view> &spellings, Word module,
                          const ExportFrame &item) const noexcept;
    // Add a module's descriptor, records, funs and exports to the indexes; on failure remove them again and rethrow.
    void index(const ModuleAtoms &atoms);
    // Remove the index entries of a module that index() added.
    void unindex(const ModuleAtoms &atoms) noexcept;
    // Index a new module and add it to modules_; a failure leaves neither changed.
    void publish(const std::shared_ptr<const LoadedModule> &module);
    // Guards modules_ and external_funs_: shared for lookups, exclusive for publication.
    mutable std::shared_mutex mutex_;
    // One registry per exact module spelling; no secondary BIF overload table exists.
    std::map<std::string, std::shared_ptr<const LoadedModule>, std::less<>> modules_;
    // External funs built from runtime operands, by module, function and arity; fun cells point at them.
    std::map<FunctionAtoms, std::unique_ptr<FunDefinition>> external_funs_;
    // Indexes built at registration (plan step 62A), so lookups take constant expected time in the number of
    // modules and exports: module descriptors to their bindings, record and fun descriptors to their definitions,
    // and Module:Function/Arity atoms to export frames. Entries point into modules_, which never shrinks.
    std::unordered_map<const void *, const ModuleAtoms *> modules_by_descriptor_;
    std::unordered_map<const void *, const RecordDefinition *> records_;
    std::unordered_map<const void *, const FunDefinition *> funs_;
    std::unordered_map<FunctionAtoms, const void *, FunctionAtomsHash> exports_;
    // Registered once at runtime startup; external fun definitions may point at its frames.
    BuiltinRegistry builtins_;
};
} // namespace clause::runtime
