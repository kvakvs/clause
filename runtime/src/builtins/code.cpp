#include "typed.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/code_server.hpp>
#include <optional>
#include <terms.hpp>

// The code module's answers for linked code (plan step 41Z, docs/builtins.md#code-module): every module is part of
// the executable, so no code server process is needed. ensure_loaded/1, ensure_modules_loaded/1, is_loaded/1,
// all_loaded/0, which/1, module_status/0,1 and is_sticky/1; a module is loaded when the program has it, as
// preloaded code, or when clause_interp interprets it, from its file (plan 21 LIB77B). A module argument that is not
// an atom has no clause, as in OTP.
namespace clause::runtime::builtins {
namespace {
// The spelling of a module argument; function_clause unless it is an atom.
std::string_view module_name(const Term &module) {
    if (!module.is_atom()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
    }
    return module.atom_spelling_unchecked();
}

// Whether the program has the module: compiled code, runtime builtins or an interpreted module.
bool loaded(ProcessContext &context, const Term &module) {
    const auto name = module_name(module);
    auto &server = context.code_server();
    return server.find_module(name).has_value() || std::ranges::contains(server.builtins().modules(), name) ||
           server.interpreted().contains(module.word());
}

// The character list of a file name.
TermResult<Term> char_list(ProcessContext &context, const std::u32string &codes) {
    TermFactory factory(context);
    std::vector<Term> items;
    items.reserve(codes.size());
    for (const auto code : codes) {
        items.push_back(need(factory.integer(static_cast<std::int64_t>(code))));
    }
    return factory.list(items);
}

// The file an interpreted module came from, as code:which/1 shows it; none for any other module.
std::optional<Term> interpreted_file(ProcessContext &context, const Term &module) {
    if (const auto file = context.code_server().interpreted().file(module.word())) {
        return need(char_list(context, *file));
    }
    return std::nullopt;
}

// A pair {First, Second}.
TermResult<Term> pair(ProcessContext &context, const Term &first, const Term &second) {
    const std::array parts{first, second};
    return TermFactory(context).tuple(parts);
}

// ensure_loaded(Module): {module, Module}, or {error, nofile} for a module the program does not have.
TermResult<Term> ensure_loaded(ProcessContext &context, const Term &module) {
    return loaded(context, module) ? pair(context, atom(context, "module"_atom), module)
                                   : pair(context, atom(context, "error"_atom), atom(context, "nofile"_atom));
}

// ensure_modules_loaded(Modules): ok, or {error, [{Module, nofile}]} for the modules the program does not have.
TermResult<Term> ensure_modules_loaded(ProcessContext &context, const Term &modules) {
    std::vector<Term> missing;
    auto rest = modules;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto module = rest.head_unchecked();
        if (!loaded(context, module)) {
            missing.push_back(need(pair(context, module, atom(context, "nofile"_atom))));
        }
    }
    if (!rest.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
    }
    if (missing.empty()) {
        return atom(context, "ok"_atom);
    }
    return pair(context, atom(context, "error"_atom), need(TermFactory(context).list(missing)));
}

// is_loaded(Module): {file, preloaded}, {file, File} for an interpreted module, or false.
TermResult<Term> is_loaded(ProcessContext &context, const Term &module) {
    if (!loaded(context, module)) {
        return atom(context, "false"_atom);
    }
    const auto file = interpreted_file(context, module);
    return pair(context, atom(context, "file"_atom), file ? *file : atom(context, "preloaded"_atom));
}

// An interpreted module's module_status/1: OTP compares the object code at its file with the loaded code, so a module
// from a source file is removed, and one from a .beam file (its debug_info chunk) loaded.
Term interpreted_status(ProcessContext &context, const std::u32string &file) {
    return atom(context, file.ends_with(U".beam") ? "loaded"_atom : "removed"_atom);
}

// [{Module, Value}] for every module of the program, then the interpreted ones with their files (when `files`) or
// their module status.
TermResult<Term> every_module(ProcessContext &context, AtomName value, bool files) {
    std::vector<Term> items;
    for (const auto &name : context.code_server().module_names()) {
        items.push_back(need(pair(context, atom(context, name), atom(context, value))));
    }
    for (const auto &[module, file] : context.code_server().interpreted().modules()) {
        const auto shown = files ? need(char_list(context, file)) : interpreted_status(context, file);
        items.push_back(need(pair(context, need(Term::from_word(module, context)), shown)));
    }
    return TermFactory(context).list(items);
}

// all_loaded(): [{Module, preloaded}], interpreted modules with their files.
TermResult<Term> all_loaded(ProcessContext &context) { return every_module(context, "preloaded"_atom, true); }

// which(Module): preloaded, an interpreted module's file, or non_existing.
TermResult<Term> which(ProcessContext &context, const Term &module) {
    if (!loaded(context, module)) {
        return atom(context, "non_existing"_atom);
    }
    const auto file = interpreted_file(context, module);
    return file ? *file : atom(context, "preloaded"_atom);
}

// module_status(Module): loaded or not_loaded; linked code is never modified or removed.
TermResult<Term> module_status1(ProcessContext &context, const Term &module) {
    if (const auto file = context.code_server().interpreted().file(module.word())) {
        return interpreted_status(context, *file);
    }
    return atom(context, loaded(context, module) ? "loaded"_atom : "not_loaded"_atom);
}

// module_status(): [{Module, loaded}], interpreted modules with their status.
TermResult<Term> module_status0(ProcessContext &context) { return every_module(context, "loaded"_atom, false); }

// is_sticky(Module): false, since no directory of the executable is sticky.
TermResult<Term> is_sticky(ProcessContext &context, const Term &module) {
    static_cast<void>(module_name(module));
    return atom(context, "false"_atom);
}

constexpr std::array CODE_MODULE_BUILTINS{
    typed_entry<ensure_loaded>("code", "ensure_loaded"),
    typed_entry<ensure_modules_loaded>("code", "ensure_modules_loaded"),
    typed_entry<is_loaded>("code", "is_loaded"),
    typed_entry<all_loaded>("code", "all_loaded"),
    typed_entry<which>("code", "which"),
    typed_entry<module_status0>("code", "module_status"),
    typed_entry<module_status1>("code", "module_status"),
    typed_entry<is_sticky>("code", "is_sticky"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> code_module_builtins() noexcept { return builtins::CODE_MODULE_BUILTINS; }
} // namespace clause::runtime
