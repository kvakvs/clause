#include "../portions.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>

// module_loaded/1, loaded/0, pre_loaded/0, delete_module/1, prepare_loading/2, finish_loading/1 and
// get_module_info/1,2 (plan step 41H, docs/builtins.md#code-loading). Every module is linked into the program and
// stays loaded (step 71 decides about dynamic code): loaded/0 and pre_loaded/0 list them all, no binary can be
// prepared for loading, and deleting a loaded module raises notsup.
namespace clause::runtime::builtins {
namespace {
// Whether a module of the program has the name: registered code or builtins.
bool loaded_module(ProcessContext &context, std::string_view name) {
    auto &server = context.code_server();
    return server.find_module(name).has_value() || std::ranges::contains(server.builtins().modules(), name);
}

// module_loaded(Module): also true for an interpreted module (plan 21 LIB77B).
TermResult<Term> module_loaded(ProcessContext &context, const AtomArgument &module) {
    return TermFactory(context).boolean(loaded_module(context, module.spelling) ||
                                        context.code_server().interpreted().route(module.term.word()) != nullptr);
}

// Every module of the program, then the interpreted ones, as atoms.
TermResult<Term> loaded(ProcessContext &context) {
    std::vector<Term> items;
    for (const auto &name : context.code_server().module_names()) {
        items.push_back(atom(context, name));
    }
    for (const auto &entry : context.code_server().interpreted().modules()) {
        items.push_back(need(Term::from_word(entry.first, context)));
    }
    return TermFactory(context).list(items);
}

// delete_module(Module): undefined for a module the program does not have; a linked module cannot be deleted.
TermResult<Term> delete_module(ProcessContext &context, const AtomArgument &module) {
    if (!loaded_module(context, module.spelling)) {
        return atom(context, "undefined"_atom);
    }
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, "notsup"_atom).word()};
}

// prepare_loading(Module, Code): {error, badfile}, since no binary is code this program can load.
TermResult<Term> prepare_loading(ProcessContext &context, const AtomArgument &, const Term &code) {
    if (!code.is_binary() && !code.is_list()) {
        bad_argument();
    }
    const std::array parts{atom(context, "error"_atom), atom(context, "badfile"_atom)};
    return TermFactory(context).tuple(parts);
}

// finish_loading(PreparedCodeList): ok for the only list prepare_loading/2 allows, [].
TermResult<Term> finish_loading(ProcessContext &context, const ListArgument &prepared) {
    if (!prepared.elements.empty()) {
        bad_argument();
    }
    return TermFactory(context).atom("ok"_atom);
}

// get_module_info(Module[, Item]): continue as Module:module_info(...), which every compiled module has; badarg
// for a module without it.
Word module_info(ProcessContext &context, Arguments arguments, std::size_t arity) {
    const auto module = term_of(context, arguments[0]);
    if (!module.is_atom()) {
        bad_argument();
    }
    const auto *frame = static_cast<const abi::v1::FrameDescriptor *>(
        context.code_server().function_frame(module, atom(context, "module_info"_atom), arity));
    if (!frame) {
        bad_argument();
    }
    context.stack().trap(*frame, arguments.subspan(1, arity));
    return 0;
}

Word get_module_info1(ProcessContext &context, Arguments arguments) { return module_info(context, arguments, 0); }

Word get_module_info2(ProcessContext &context, Arguments arguments) { return module_info(context, arguments, 1); }

constexpr std::array CODE_BUILTINS{
    typed_entry<module_loaded>("erlang", "module_loaded"),
    typed_entry<loaded>("erlang", "loaded"),
    typed_entry<loaded>("erlang", "pre_loaded"),
    typed_entry<delete_module>("erlang", "delete_module"),
    typed_entry<prepare_loading>("erlang", "prepare_loading"),
    typed_entry<finish_loading>("erlang", "finish_loading"),
    BuiltinEntry{"erlang", "get_module_info", 1, &guarded<get_module_info1>},
    BuiltinEntry{"erlang", "get_module_info", 2, &guarded<get_module_info2>},
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> code_builtins() noexcept { return builtins::CODE_BUILTINS; }
} // namespace clause::runtime
