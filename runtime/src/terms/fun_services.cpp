#include "../memory/heap_object.hpp"
#include "funs.hpp"
#include "service_errors.hpp"
#include "term_layout.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <clause/abi/calls.hpp>
#include <clause/abi/frames.hpp>
#include <clause/abi/funs.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <vector>

// CLAUSE_make_fun_v1 and CLAUSE_apply_v1: building funs and preparing their calls (docs/funs.md).
namespace clause::runtime::detail {
using abi::v1::ErrorReason;
using abi::v1::Status;

bool service_ready(ProcessContext &context) {
    const auto &calls = context.generated_calls();
    return calls.active() && !calls.failure();
}

void raise_call_error(ProcessContext &context, ErrorReason reason, std::optional<Term> value) {
    context.generated_calls().fail({.code = CallError::erlang_exception, .reason = reason, .value = value});
}

namespace {

// Build a fun of a registered descriptor from rooted captured values.
Status make(ProcessContext &context, const void *descriptor, std::span<const Word> captures, Word &output) {
    const auto *definition = context.code_server().fun_definition(descriptor);
    if (!definition) {
        return Status::wrong_owner;
    }
    const auto fun = TermFactory(context).fun_words(*definition, captures);
    if (!fun) {
        return term_status(fun.error());
    }
    output = fun->word();
    return Status::ok;
}

// The {Fun, Args} payload of badarity, with the arguments in the registers.
TermResult<Term> bad_arity(ProcessContext &context, const Term &fun, std::span<const Word> arguments) {
    TermFactory factory(context);
    std::vector<Term> items;
    items.reserve(arguments.size());
    for (const auto word : arguments) {
        auto item = Term::from_word(word, context);
        if (!item) {
            return item;
        }
        items.push_back(*item);
    }
    const auto list = factory.list(items);
    return list.and_then([&](const Term &args) { return factory.tuple(std::array{fun, args}); });
}

} // namespace

const void *prepare_fun_call(ProcessContext &context, const Term &fun, std::size_t arity, Word *arguments) {
    const auto view = fun_view(fun);
    if (!view) {
        raise_call_error(context, ErrorReason::badfun, fun);
        return nullptr;
    }
    const auto &definition = *view->definition;
    if (definition.arity != arity) {
        const auto payload = bad_arity(context, fun, {arguments, arity});
        if (payload) {
            raise_call_error(context, ErrorReason::badarity, *payload);
        } else {
            context.generated_calls().fail_service(term_status(payload.error()));
        }
        return nullptr;
    }
    if (!definition.frame) {
        raise_call_error(context, ErrorReason::undef);
        return nullptr;
    }
    std::ranges::copy(view->captures, arguments + arity);
    return definition.frame;
}

namespace {
// Check the invocation and pointers, then build the fun; any failure is recorded in the channel.
std::uint8_t make_service(ProcessContext &context, const void *descriptor, const Word *captures, std::size_t count,
                          Word *output) noexcept {
    if (!service_ready(context)) {
        return static_cast<std::uint8_t>(Status::invalid_argument);
    }
    auto status = Status::invalid_argument;
    try {
        if (output && (count == 0 || captures)) {
            status = make(context, descriptor, {captures, count}, *output);
        }
    } catch (const std::bad_alloc &) {
        status = Status::out_of_memory;
    } catch (...) {
        status = Status::internal_error;
    }
    if (status != Status::ok) {
        context.generated_calls().fail_service(status);
    }
    return static_cast<std::uint8_t>(status);
}

} // namespace

const void *word_apply(ProcessContext &context, Word fun, Word *arguments, std::size_t arity) noexcept {
    if ((fun & abi::v1::primary_mask) != abi::v1::boxed_tag || !arguments) {
        return nullptr;
    }
    const auto words = TermAccess::boxed_words(context.heap(), fun);
    if (words.empty() || layout::BoxHeader::kind(words[0]) != BoxedKind::fun_closure) {
        return nullptr;
    }
    const auto &definition = *reinterpret_cast<const layout::FunCell *>(words.data())->definition_;
    const auto captures = words.subspan(2);
    if (definition.arity != arity || !definition.frame || arity + captures.size() > abi::v1::register_count) {
        return nullptr;
    }
    std::ranges::copy(captures, arguments + arity);
    return definition.frame;
}

namespace {
// Admit the called value and check the register bounds before reading any argument.
const void *apply(ProcessContext &context, Word fun, Word *arguments, std::size_t arity) noexcept {
    if (!service_ready(context)) {
        return nullptr;
    }
    if (const auto *frame = word_apply(context, fun, arguments, arity)) {
        return frame;
    }
    auto &calls = context.generated_calls();
    try {
        const auto value = Term::from_word(fun, context);
        if (!value) {
            calls.fail_service(term_status(value.error()));
            return nullptr;
        }
        const auto view = fun_view(*value);
        const auto captures = view ? view->captures.size() : 0;
        if (!arguments || arity + captures > abi::v1::register_count) {
            calls.fail_service(Status::invalid_argument);
            return nullptr;
        }
        return prepare_fun_call(context, *value, arity, arguments);
    } catch (const std::bad_alloc &) {
        calls.fail_service(Status::out_of_memory);
    } catch (...) {
        calls.fail_service(Status::internal_error);
    }
    return nullptr;
}
} // namespace
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_make_fun_v1(void *context, const void *descriptor, const clause::abi::v1::TermWord *captures,
                                std::size_t count, clause::abi::v1::TermWord *output) noexcept {
    if (!context) {
        return static_cast<std::uint8_t>(clause::abi::v1::Status::invalid_argument);
    }
    return clause::runtime::detail::make_service(*static_cast<clause::runtime::ProcessContext *>(context), descriptor,
                                                 captures, count, output);
}

const void *CLAUSE_apply_v1(void *context, clause::abi::v1::TermWord fun, std::size_t arity,
                            clause::abi::v1::TermWord *arguments) noexcept {
    if (!context) {
        return nullptr;
    }
    return clause::runtime::detail::apply(*static_cast<clause::runtime::ProcessContext *>(context), fun, arguments,
                                          arity);
}
