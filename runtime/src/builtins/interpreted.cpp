#include "../terms/funs.hpp"
#include "support.hpp"
#include "typed.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>

// clause_interp's runtime half (plan 21 LIB77B, docs/builtins.md#module-interpreter): the registry of interpreted
// modules that calls, code queries and function_exported/3 consult. Added for the module interpreter.
namespace clause::runtime::builtins {
namespace {
using File = Named<struct FileRole>;
using Route = Named<struct RouteRole>;

// The code points of a flat character list; badarg for anything else.
std::u32string file_name(const File &file) {
    std::u32string codes;
    auto rest = file.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto element = rest.head_unchecked();
        const auto code = element.is_integer() ? element.integer_value() : TermResult<std::int64_t>{-1};
        if (!code || *code < 0 || *code > 0x10FFFF) {
            bad_argument();
        }
        codes.push_back(static_cast<char32_t>(*code));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return codes;
}

// One {Function, Arity} pair of an export list; badarg for anything else.
InterpretedModules::Export export_pair(const Term &item) {
    const auto parts = item.is_tuple() ? item.tuple_elements() : TermResult<std::vector<Term>>{};
    if (!parts || parts->size() != 2 || !(*parts)[0].is_atom() || !(*parts)[1].is_integer()) {
        bad_argument();
    }
    const auto arity = (*parts)[1].integer_value();
    if (!arity || *arity < 0 || *arity > 255) {
        bad_argument();
    }
    return {(*parts)[0].word(), static_cast<std::size_t>(*arity)};
}

// The frame of the route fun, error_handler:undefined_function/3; badarg for anything else.
const void *route_frame(const Route &route) {
    const auto view = route.term.is_function() ? detail::fun_view(route.term) : TermResult<detail::FunView>{};
    if (!view || !view->definition || !view->definition->frame || view->definition->arity != 3) {
        bad_argument();
    }
    return view->definition->frame;
}

// Whether the program links a module of this name: compiled code or runtime builtins.
bool linked(ProcessContext &context, std::string_view name) {
    auto &server = context.code_server();
    return server.find_module(name).has_value() || std::ranges::contains(server.builtins().modules(), name);
}

// register(Module, File, Exports, Route): calls of Module enter Route's function (error_handler:undefined_function/3)
// from now on; `linked` instead when the program links a module of that name.
TermResult<Term> register_module(ProcessContext &context, const AtomArgument &module, const File &file,
                                 const ListArgument &exports, const Route &route) {
    const auto *frame = route_frame(route);
    if (linked(context, module.spelling)) {
        return atom(context, "linked"_atom);
    }
    InterpretedModules::Module entry{.file = file_name(file), .exports = {}};
    for (const auto &item : exports.elements) {
        entry.exports.insert(export_pair(item));
    }
    context.code_server().interpreted().add(module.term.word(), std::move(entry), frame);
    return atom(context, "ok"_atom);
}

// unregister(Module): true when Module was interpreted.
TermResult<Term> unregister_module(ProcessContext &context, const AtomArgument &module) {
    return TermFactory(context).boolean(context.code_server().interpreted().remove(module.term.word()));
}

// route_undefined(Route): every call of an undefined function enters Route's function from now on (plan 21 LIB77C).
TermResult<Term> route_undefined(ProcessContext &context, const Route &route) {
    context.code_server().interpreted().route_undefined(route_frame(route));
    return atom(context, "ok"_atom);
}

// interpreted(Module): whether Module is interpreted.
TermResult<Term> interpreted(ProcessContext &context, const AtomArgument &module) {
    return TermFactory(context).boolean(context.code_server().interpreted().contains(module.term.word()));
}

constexpr std::array INTERPRETER_BUILTINS{
    typed_entry<register_module>("clause_interp", "register"),
    typed_entry<unregister_module>("clause_interp", "unregister"),
    typed_entry<interpreted>("clause_interp", "interpreted"),
    typed_entry<route_undefined>("clause_interp", "route_undefined"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> interpreter_builtins() noexcept { return builtins::INTERPRETER_BUILTINS; }
} // namespace clause::runtime
