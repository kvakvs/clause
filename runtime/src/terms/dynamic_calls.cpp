#include "../memory/heap_object.hpp"
#include "funs.hpp"
#include "service_errors.hpp"
#include "terms.hpp"
#include <array>
#include <clause/abi/equality.hpp>
#include <clause/abi/frames.hpp>
#include <clause/abi/funs.hpp>
#include <clause/abi/term.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>

// Dynamic calls (docs/funs.md#dynamic-calls): M:F(Args) and apply/2,3 with runtime operands, and fun M:F/A with
// variables. Each service records its Erlang error or failure in the checked channel.
namespace clause::runtime::detail {
namespace {
using abi::v1::ErrorReason;
using abi::v1::Status;

// Run a call service body when the invocation is ready, containing host exceptions as service failures.
template <typename Body> auto contained(ProcessContext &context, Body body) noexcept -> decltype(body()) {
    if (!service_ready(context)) {
        return {};
    }
    auto &calls = context.generated_calls();
    try {
        return body();
    } catch (const std::bad_alloc &) {
        calls.fail_service(Status::out_of_memory);
    } catch (...) {
        calls.fail_service(Status::internal_error);
    }
    return {};
}

// Whether a register array or output slot was given; false after recording the failure.
bool present(ProcessContext &context, const Word *pointer) {
    if (!pointer) {
        context.generated_calls().fail_service(Status::invalid_argument);
    }
    return pointer != nullptr;
}

// Admit the given words as terms of this process; false after recording the failure.
template <std::size_t N>
bool admit(ProcessContext &context, const std::array<Word, N> &words, std::array<Term, N> &out) {
    for (std::size_t i = 0; i < N; ++i) {
        auto term = Term::from_word(words[i], context);
        if (!term) {
            context.generated_calls().fail_service(term_status(term.error()));
            return false;
        }
        out[i] = std::move(*term);
    }
    return true;
}

// Copy a proper list's elements into the registers while they fit; its length, or none for an improper list.
TermResult<std::optional<std::size_t>> unpack(Term list, Word *registers) {
    std::size_t count = 0;
    while (list.is_cons()) {
        const auto head = list.head();
        auto tail = list.tail();
        if (!head || !tail) {
            return std::unexpected(head ? tail.error() : head.error());
        }
        if (count < abi::v1::register_count) {
            registers[count] = head->word();
        }
        ++count;
        list = *tail;
    }
    return list.is_nil() ? std::optional{count} : std::nullopt;
}

// Copy a proper list's elements into the registers from its cons cells, with no Term per cell; its length, or none
// for an improper list, one with more elements than registers, or a cell this process does not hold (cheap fun
// calls), which the checked path then reports.
std::optional<std::size_t> unpack_words(ProcessContext &context, Word list, Word *registers) noexcept {
    std::size_t count = 0;
    for (; (list & abi::v1::primary_mask) == abi::v1::list_tag; ++count) {
        const auto cell = TermAccess::cons_words(context.heap(), list);
        if (cell.empty() || count == abi::v1::register_count) {
            return std::nullopt;
        }
        registers[count] = cell[0];
        list = cell[1];
    }
    return list == abi::v1::empty_list ? std::optional{count} : std::nullopt;
}

// The operands of apply(Fun, Args).
struct ApplyOperands {
    Word fun;
    Word list;
};

// apply(Fun, Args) of a fun of the right arity and a proper list that fit the registers, from words alone; null
// for the checked path.
const void *word_apply_list(ProcessContext &context, const ApplyOperands operands, Word *registers) noexcept {
    if (!registers || !service_ready(context)) {
        return nullptr;
    }
    const auto count = unpack_words(context, operands.list, registers);
    return count ? word_apply(context, operands.fun, registers, *count) : nullptr;
}

// The frame Module:Function/Arity enters: badarg for a non-atom name, undef when neither a module exports it nor
// a builtin has that name.
const void *lookup(ProcessContext &context, const Term &module, const Term &function, std::size_t arity) {
    if (!module.is_atom() || !function.is_atom()) {
        raise_call_error(context, ErrorReason::badarg);
        return nullptr;
    }
    const auto *frame = context.code_server().function_frame(module, function, arity);
    if (!frame) {
        raise_call_error(context, ErrorReason::undef);
    }
    return frame;
}

// apply(Fun, Args) once Args is unpacked: a fun of another arity raises badarity with the original list.
const void *apply_fun(ProcessContext &context, const Term &fun, const Term &list, std::size_t count, Word *registers) {
    const auto view = fun_view(fun);
    if (view && view->definition->arity != count) {
        const auto payload = TermFactory(context).tuple(std::array{fun, list});
        if (payload) {
            raise_call_error(context, ErrorReason::badarity, *payload);
        } else {
            context.generated_calls().fail_service(term_status(payload.error()));
        }
        return nullptr;
    }
    return prepare_fun_call(context, fun, count, registers);
}

// Unpack Args into the registers and return its length; none after raising badarg for an improper list or after
// a failure.
std::optional<std::size_t> arguments(ProcessContext &context, const Term &list, Word *registers) {
    const auto count = unpack(list, registers);
    if (!count) {
        context.generated_calls().fail_service(term_status(count.error()));
        return std::nullopt;
    }
    if (!*count) {
        raise_call_error(context, ErrorReason::badarg);
        return std::nullopt;
    }
    return *count;
}

// Build fun M:F/A from runtime operands: badarg unless M and F are atoms and A is an integer in 0..255.
Status make_external(ProcessContext &context, const std::array<Term, 3> &operands, Word &output) {
    const auto &[module, function, arity] = operands;
    const auto count = arity.is_integer() ? arity.integer_value() : TermResult<std::int64_t>{-1};
    if (!module.is_atom() || !function.is_atom() || !count || *count < 0 || *count > 255) {
        raise_call_error(context, ErrorReason::badarg);
        return Status::erlang_error;
    }
    const auto &definition = context.code_server().external_fun(module, function, static_cast<std::size_t>(*count));
    const auto fun = TermFactory(context).fun_words(definition, {});
    if (!fun) {
        return term_status(fun.error());
    }
    output = fun->word();
    return Status::ok;
}
} // namespace

const void *call_service(ProcessContext &context, Word module, Word function, std::size_t arity) noexcept {
    return contained(context, [&]() -> const void * {
        std::array<Term, 2> names;
        return admit(context, std::array{module, function}, names) ? lookup(context, names[0], names[1], arity)
                                                                   : nullptr;
    });
}

const void *apply_list_service(ProcessContext &context, Word fun, Word list, Word *registers) noexcept {
    if (const auto *frame = word_apply_list(context, {fun, list}, registers)) {
        return frame;
    }
    return contained(context, [&]() -> const void * {
        std::array<Term, 2> terms;
        if (!present(context, registers) || !admit(context, std::array{fun, list}, terms)) {
            return nullptr;
        }
        const auto count = arguments(context, terms[1], registers);
        return count ? apply_fun(context, terms[0], terms[1], *count, registers) : nullptr;
    });
}

const void *call_list_service(ProcessContext &context, Word module, Word function, Word list,
                              Word *registers) noexcept {
    return contained(context, [&]() -> const void * {
        std::array<Term, 3> terms;
        if (!present(context, registers) || !admit(context, std::array{module, function, list}, terms)) {
            return nullptr;
        }
        const auto count = arguments(context, terms[2], registers);
        return count ? lookup(context, terms[0], terms[1], *count) : nullptr;
    });
}

std::uint8_t make_external_service(ProcessContext &context, Word module, Word function, Word arity,
                                   Word *output) noexcept {
    if (!service_ready(context)) {
        return static_cast<std::uint8_t>(Status::invalid_argument);
    }
    const auto status = contained(context, [&]() -> std::optional<Status> {
        std::array<Term, 3> operands;
        if (!present(context, output) || !admit(context, std::array{module, function, arity}, operands)) {
            return std::nullopt;
        }
        return make_external(context, operands, *output);
    });
    if (status && *status != Status::ok && *status != Status::erlang_error) {
        context.generated_calls().fail_service(*status);
    }
    return static_cast<std::uint8_t>(status.value_or(Status::internal_error));
}
} // namespace clause::runtime::detail

const void *CLAUSE_call_v1(void *context, clause::abi::v1::TermWord module, clause::abi::v1::TermWord function,
                           std::size_t arity) noexcept {
    if (!context) {
        return nullptr;
    }
    return clause::runtime::detail::call_service(*static_cast<clause::runtime::ProcessContext *>(context), module,
                                                 function, arity);
}

const void *CLAUSE_apply_list_v1(void *context, clause::abi::v1::TermWord fun, clause::abi::v1::TermWord list,
                                 clause::abi::v1::TermWord *registers) noexcept {
    if (!context) {
        return nullptr;
    }
    return clause::runtime::detail::apply_list_service(*static_cast<clause::runtime::ProcessContext *>(context), fun,
                                                       list, registers);
}

const void *CLAUSE_call_list_v1(void *context, clause::abi::v1::TermWord module, clause::abi::v1::TermWord function,
                                clause::abi::v1::TermWord list, clause::abi::v1::TermWord *registers) noexcept {
    if (!context) {
        return nullptr;
    }
    return clause::runtime::detail::call_list_service(*static_cast<clause::runtime::ProcessContext *>(context), module,
                                                      function, list, registers);
}

std::uint8_t CLAUSE_make_external_fun_v1(void *context, clause::abi::v1::TermWord module,
                                         clause::abi::v1::TermWord function, clause::abi::v1::TermWord arity,
                                         clause::abi::v1::TermWord *output) noexcept {
    if (!context) {
        return static_cast<std::uint8_t>(clause::abi::v1::Status::invalid_argument);
    }
    return clause::runtime::detail::make_external_service(*static_cast<clause::runtime::ProcessContext *>(context),
                                                          module, function, arity, output);
}
