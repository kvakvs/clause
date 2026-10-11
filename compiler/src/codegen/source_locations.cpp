#include "source_locations.hpp"
#include "../semantic/declarations.hpp"
#include <limits>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/TargetParser/Triple.h>
#include <stdexcept>

namespace clause::codegen {
namespace {
// Reject unrepresentable line numbers instead of attaching a different source line after narrowing.
unsigned line_number(const Span &site) {
    const auto line = site.source->position(site.begin).line;
    if (line > std::numeric_limits<unsigned>::max()) {
        throw std::length_error("source line exceeds LLVM location limit");
    }
    return static_cast<unsigned>(line);
}

struct Definition {
    // The Erlang name, native symbol and source extent of a function or anonymous fun.
    std::u32string name;
    const std::string &symbol;
    const ast::NodeSource &source;
};

// Preserve the original Erlang identity and physical declaration file in each definition's scope.
void function_scope(llvm::DIBuilder &debug, llvm::Module &output, const semantic::Module &module,
                    const Definition &definition, const bool optimized, SourceScopes &sources) {
    const auto &site = source_site(module.syntax->anchor(definition.source));
    auto *file = debug.createFile(site.source->name, "");
    auto *type = debug.createSubroutineType(debug.getOrCreateTypeArray({}));
    const auto line = line_number(site);
    const auto flags = llvm::DISubprogram::toSPFlags(false, true, optimized);
    // No linkage name: GDB would name frames by the native symbol instead of the Erlang name.
    auto *scope =
        debug.createFunction(file, utf8(definition.name), "", file, line, type, line, llvm::DINode::FlagZero, flags);
    output.getFunction(definition.symbol)->setSubprogram(scope);
    for (const auto &origin : module.syntax->extent(definition.source)) {
        const auto &physical = source_site(origin);
        auto *source_file = debug.createFile(physical.source->name, "");
        auto *block = llvm::DILexicalBlockFile::get(output.getContext(), scope, source_file, 0);
        sources.try_emplace(block, physical.source);
    }
}
} // namespace

const Span &source_site(const ast::TokenOrigin &origin) {
    // Macro ancestry consists of definition/invocation pairs, starting with the outer invocation.
    if (origin.related.size() >= 2) {
        return origin.related[1];
    }
    return origin.related.empty() ? origin.spelling : origin.related.front();
}

void request_debug_format(llvm::Module &output) {
    if (llvm::Triple(output.getTargetTriple()).isWindowsMSVCEnvironment()) {
        output.addModuleFlag(llvm::Module::Warning, "CodeView", 1);
    } else {
        output.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
    }
}

void prepare_source_locations(llvm::Module &output, const semantic::Module &module, const bool optimized,
                              SourceScopes &sources) {
    llvm::DIBuilder debug(output);
    auto *file = debug.createFile(output.getSourceFileName(), "");
    // Erlang uses a private language code here; these scopes describe source lines, not debugger types. Full
    // emission keeps the named function scopes in DWARF, which drops them from line-only units that inline nothing.
    debug.createCompileUnit(llvm::dwarf::DW_LANG_lo_user, file, "clau", optimized, "", 0, "",
                            llvm::DICompileUnit::FullDebug);
    output.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
    for (const auto &function : module.functions) {
        // OTP's predefined functions have no source line; without a scope they get no locations or comments.
        if (semantic::predefined_form(module, function.form) || !function.live_) {
            continue;
        }
        const auto &source = module.syntax->form(function.form).source;
        function_scope(debug, output, module, {function.key.name, function.symbol, source}, optimized, sources);
    }
    for (const auto &fun : module.funs) {
        if (fun.expression && fun.owner->live_) {
            function_scope(debug, output, module, {fun.function, fun.symbol, fun.expression->source}, optimized,
                           sources);
        }
    }
    debug.finalize();
}

void locate_source(llvm::IRBuilder<> &builder, const ast::Module &syntax, const ast::NodeSource &source) {
    const auto *function = builder.GetInsertBlock()->getParent();
    auto *scope = function->getSubprogram();
    if (!scope) {
        return;
    }
    const auto &site = source_site(syntax.anchor(source));
    auto &context = builder.getContext();
    auto *file = llvm::DIFile::get(context, site.source->name, "");
    auto *location_scope = llvm::DILexicalBlockFile::get(context, scope, file, 0);
    builder.SetCurrentDebugLocation(llvm::DILocation::get(context, line_number(site), 0, location_scope));
}
} // namespace clause::codegen
