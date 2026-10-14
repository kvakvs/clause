#include "typed.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/code_server.hpp>
#include <terms.hpp>

// The code module's answers for linked code (plan step 41Z, docs/builtins.md#code-module): every module is part of
// the executable, so no code server process is needed. ensure_loaded/1, ensure_modules_loaded/1, is_loaded/1,
// all_loaded/0, which/1, module_status/0,1 and is_sticky/1; a module is loaded when the program has it, as
// preloaded code. A module argument that is not an atom has no clause, as in OTP.
namespace clause::runtime::builtins {
namespace {
// The atom `name`.
Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

// The spelling of a module argument; function_clause unless it is an atom.
std::string_view module_name(const Term &module) {
    if (!module.is_atom()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
    }
    return need(module.atom_spelling());
}

// Whether the program has the module: compiled code or runtime builtins.
bool loaded(ProcessContext &context, std::string_view name) {
    auto &server = context.code_server();
    return server.find_module(name).has_value() || std::ranges::contains(server.builtins().modules(), name);
}

// A pair {First, Second}.
TermResult<Term> pair(ProcessContext &context, const Term &first, const Term &second) {
    const std::array parts{first, second};
    return TermFactory(context).tuple(parts);
}

// ensure_loaded(Module): {module, Module}, or {error, nofile} for a module the program does not have.
TermResult<Term> ensure_loaded(ProcessContext &context, const Term &module) {
    return loaded(context, module_name(module)) ? pair(context, atom(context, "module"), module)
                                                : pair(context, atom(context, "error"), atom(context, "nofile"));
}

// ensure_modules_loaded(Modules): ok, or {error, [{Module, nofile}]} for the modules the program does not have.
TermResult<Term> ensure_modules_loaded(ProcessContext &context, const Term &modules) {
    std::vector<Term> missing;
    auto rest = modules;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        const auto module = need(rest.head());
        if (!loaded(context, module_name(module))) {
            missing.push_back(need(pair(context, module, atom(context, "nofile"))));
        }
    }
    if (!rest.is_nil()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::function_clause};
    }
    if (missing.empty()) {
        return atom(context, "ok");
    }
    return pair(context, atom(context, "error"), need(TermFactory(context).list(missing)));
}

// is_loaded(Module): {file, preloaded}, or false.
TermResult<Term> is_loaded(ProcessContext &context, const Term &module) {
    return loaded(context, module_name(module)) ? pair(context, atom(context, "file"), atom(context, "preloaded"))
                                                : atom(context, "false");
}

// [{Module, Value}] for every module of the program.
TermResult<Term> every_module(ProcessContext &context, std::string_view value) {
    std::vector<Term> items;
    for (const auto &name : context.code_server().module_names()) {
        items.push_back(need(pair(context, atom(context, name), atom(context, value))));
    }
    return TermFactory(context).list(items);
}

// all_loaded(): [{Module, preloaded}].
TermResult<Term> all_loaded(ProcessContext &context) { return every_module(context, "preloaded"); }

// which(Module): preloaded, or non_existing.
TermResult<Term> which(ProcessContext &context, const Term &module) {
    return atom(context, loaded(context, module_name(module)) ? "preloaded" : "non_existing");
}

// module_status(Module): loaded or not_loaded; linked code is never modified or removed.
TermResult<Term> module_status1(ProcessContext &context, const Term &module) {
    return atom(context, loaded(context, module_name(module)) ? "loaded" : "not_loaded");
}

// module_status(): [{Module, loaded}].
TermResult<Term> module_status0(ProcessContext &context) { return every_module(context, "loaded"); }

// is_sticky(Module): false, since no directory of the executable is sticky.
TermResult<Term> is_sticky(ProcessContext &context, const Term &module) {
    static_cast<void>(module_name(module));
    return atom(context, "false");
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
