#include "assembly_listing.hpp"
#include "assembly_annotations.hpp"
#include "bounded_stream.hpp"
#include "limits.hpp"
#include "llvm_state.hpp"
#include "progress.hpp"
#include "source_locations.hpp"
#include "verification.hpp"
#include <exception>
#include <limits>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/ModuleSlotTracker.h>
#include <llvm/MC/MCAsmInfo.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <stdexcept>

namespace clause::codegen {
namespace {
// Fold LLVM's printing of one instruction (a `switch` spans lines) to one line without its `!dbg` attachment.
std::string one_line(const std::string_view printed) {
    std::string text;
    bool newline = false;
    for (const char character : printed.substr(std::min(printed.find_first_not_of(' '), printed.size()))) {
        if (character == '\n') {
            newline = true;
        } else if (!newline || character != ' ') {
            text += newline ? " " : "";
            text += character;
            newline = false;
        }
    }
    constexpr std::string_view DEBUG_ATTACHMENT = ", !dbg !";
    if (const auto at = text.find(DEBUG_ATTACHMENT); at != std::string::npos) {
        const auto end = text.find_first_not_of("0123456789", at + DEBUG_ATTACHMENT.size());
        text.erase(at, (end == std::string::npos ? text.size() : end) - at);
    }
    return text;
}

// Replaces a clone's debug information with a line table whose line N is original instruction N, so the assembly's
// line directives lead back to the IR the instruction selector consumed.
class Numbering {
  public:
    // Strip the clone's Erlang locations; the originals keep them for the listing's Erlang comments.
    Numbering(const llvm::Module &original, llvm::Module &clone, llvm::ValueToValueMapTy &clones,
              const SourceScopes &sources)
        : clone_(clone), clones_(clones), sources_(sources), slots_(&original), debug_(clone) {
        llvm::StripDebugInfo(clone);
        file_ = debug_.createFile(original.getModuleIdentifier() + ".ll", "");
        debug_.createCompileUnit(llvm::dwarf::DW_LANG_lo_user, file_, "clau", false, "", 0, "",
                                 llvm::DICompileUnit::LineTablesOnly);
        type_ = debug_.createSubroutineType(debug_.getOrCreateTypeArray({}));
    }

    // Number every defined function's instructions in module order, then finish the clone's debug metadata.
    std::vector<IrLine> number(const llvm::Module &original) {
        std::size_t index = 0;
        for (const auto &function : original) {
            if (!function.isDeclaration()) {
                number_function(function, index++);
            }
        }
        debug_.finalize();
        clone_.setModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
        request_debug_format(clone_);
        return std::move(lines_);
    }

  private:
    // Give the function's clone its own scope and each cloned instruction its original's number.
    void number_function(const llvm::Function &function, const std::size_t index) {
        auto *scope = debug_.createFunction(file_, function.getName(), "", file_, 0, type_, 0, llvm::DINode::FlagZero,
                                            llvm::DISubprogram::SPFlagDefinition);
        counterpart(function).setSubprogram(scope);
        for (const auto &instruction : llvm::instructions(function)) {
            lines_.push_back(describe(instruction, index));
            if (lines_.size() > std::numeric_limits<unsigned>::max()) {
                throw std::length_error("assembly listing exceeds LLVM line limit");
            }
            const auto line = static_cast<unsigned>(lines_.size());
            counterpart(instruction).setDebugLoc(llvm::DILocation::get(clone_.getContext(), line, 0, scope));
        }
    }

    // The original's IR text and Erlang line.
    IrLine describe(const llvm::Instruction &instruction, const std::size_t function) {
        std::string printed;
        llvm::raw_string_ostream stream(printed);
        instruction.print(stream, slots_);
        const auto &location = instruction.getDebugLoc();
        return {.text_ = one_line(printed),
                .source_ = location ? source_line_of(location, sources_) : std::nullopt,
                .function_ = function};
    }

    // The clone of an original function or instruction.
    template <class Value> Value &counterpart(const Value &original) {
        return *llvm::cast<Value>(static_cast<llvm::Value *>(clones_[&original]));
    }

    // The module whose debug information is replaced.
    llvm::Module &clone_;
    // Maps original values to their clones.
    llvm::ValueToValueMapTy &clones_;
    // Resolves original locations to Erlang source lines.
    const SourceScopes &sources_;
    // Numbers unnamed original values once, as the whole-module IR printer does.
    llvm::ModuleSlotTracker slots_;
    // Builds the clone's line table.
    llvm::DIBuilder debug_;
    // The listing's synthetic IR file and the type shared by its function scopes.
    llvm::DIFile *file_ = nullptr;
    llvm::DISubroutineType *type_ = nullptr;
    // The described instructions, in line order.
    std::vector<IrLine> lines_;
};

// Restores the target machine's assembly verbosity after printing one listing.
class VerboseAssembly {
  public:
    explicit VerboseAssembly(llvm::TargetMachine &machine)
        : machine_(machine), previous_(machine.Options.MCOptions.AsmVerbose) {
        machine.Options.MCOptions.AsmVerbose = true;
    }
    ~VerboseAssembly() { machine_.Options.MCOptions.AsmVerbose = previous_; }
    VerboseAssembly(const VerboseAssembly &) = delete;
    VerboseAssembly &operator=(const VerboseAssembly &) = delete;

  private:
    // The batch's target machine.
    llvm::TargetMachine &machine_;
    // Its verbosity before the listing.
    bool previous_;
};

// Run the batch's machine-code pipeline on the numbered clone; verbose assembly names IR blocks beside labels.
std::string machine_assembly(detail::CompilationState &state, llvm::Module &clone, const std::size_t capacity) {
    BoundedStream stream(capacity);
    {
        const VerboseAssembly verbose(*state.target_machine);
        llvm::legacy::PassManager passes;
        if (state.target_machine->addPassesToEmitFile(passes, stream, nullptr, llvm::CodeGenFileType::AssemblyFile,
                                                      false)) {
            throw std::runtime_error("target does not support assembly emission");
        }
        passes.run(clone);
    }
    const auto bytes = stream.take_bytes();
    return {reinterpret_cast<const char *>(bytes.data()), bytes.size()};
}

// Number a clone of one module, print its assembly and comment it with the original's IR and Erlang lines.
OutputBuffer list_module(detail::CompilationState &state, const llvm::Module &module,
                         const std::span<const OutputBuffer> outputs) {
    llvm::ValueToValueMapTy clones;
    const auto clone = llvm::CloneModule(module, clones);
    const auto lines = Numbering(module, *clone, clones, state.source_scopes).number(module);
    const auto capacity = output_capacity(state.request.limits, outputs);
    const auto assembly = machine_assembly(state, *clone, capacity);
    const auto comment = state.target_machine->getMCAsmInfo().getCommentString();
    return {.module_name = module.getModuleIdentifier(),
            .kind = OutputKind::assembly,
            .bytes = annotate_assembly(assembly, lines, {comment.data(), comment.size()}, capacity),
            .startup = false};
}
} // namespace

std::optional<std::vector<OutputBuffer>> snapshot_assembly(Compilation &compilation) {
    if (!verify_ir(compilation)) {
        return {};
    }
    auto &state = detail::state(compilation);
    std::vector<OutputBuffer> outputs;
    try {
        // The startup module has no Erlang source; IR inspection omits it too.
        for (std::size_t index = 0; index < state.request.inputs.size(); ++index) {
            progress_module(compilation, index, "assembly");
            outputs.push_back(list_module(state, *state.modules[index], outputs));
            if (state.result.status() == CompilationStatus::failed) {
                return {};
            }
        }
    } catch (const std::exception &error) {
        state.result.report({.level = DiagnosticLevel::error,
                             .message = "LLVM assembly listing failed: " + std::string(error.what()),
                             .location = {},
                             .module_name = {}});
        return {};
    }
    return outputs;
}

std::string assembly_comment(const Compilation &compilation) {
    return detail::state(compilation).target_machine->getMCAsmInfo().getCommentString().str();
}
} // namespace clause::codegen
