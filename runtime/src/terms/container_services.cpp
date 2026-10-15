#include "service_errors.hpp"
#include "terms.hpp"
#include <algorithm>
#include <clause/abi/containers.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <span>
#include <stdexcept>

namespace clause::runtime::detail {
namespace {
using Outcome = abi::v1::ValueOutcome;
using Inspect = abi::v1::ContainerInspection;
using Construct = abi::v1::ContainerConstruction;

struct InspectionRequest {
    // Name the operation, candidate and raw shape/index independently of their convertible ABI scalars.
    Inspect operation;
    Word value;
    std::size_t index;
};

// Publish only successful terms; malformed/foreign words never become an ordinary pattern mismatch.
std::uint8_t publish_result(ProcessContext &context, const TermResult<Term> &result, Word *output) {
    if (result) {
        *output = result->word();
        return static_cast<std::uint8_t>(Outcome::success);
    }
    if (result.error() == TermError::wrong_type || result.error() == TermError::out_of_range) {
        return static_cast<std::uint8_t>(Outcome::bad_argument);
    }
    context.generated_calls().fail_service(term_status(result.error()));
    return static_cast<std::uint8_t>(Outcome::failure);
}

// Prepend the elements of a proper list (`values[0]`) to a tail (`values[1]`) in reverse order; an improper list is a
// bad argument.
TermResult<Term> reverse(ProcessContext &context, std::span<const Word> values) {
    if (values.size() != 2) {
        return std::unexpected(TermError::invalid_argument);
    }
    const auto list = Term::from_word(values[0], context);
    const auto tail = Term::from_word(values[1], context);
    if (!list || !tail) {
        return std::unexpected(list ? tail.error() : list.error());
    }
    return TermFactory(context).reverse(*list, *tail);
}

// Validate all input ownership before invoking a transactional factory; the tuple factory checks the arity.
TermResult<Term> construct(ProcessContext &context, Construct operation, std::span<const Word> values) {
    TermFactory factory(context);
    if (operation == Construct::tuple) {
        return factory.tuple_words(values);
    }
    if (operation == Construct::reverse) {
        return reverse(context, values);
    }
    std::vector<Term> terms;
    terms.reserve(values.size());
    for (const auto value : values) {
        const auto term = Term::from_word(value, context);
        if (!term) {
            return std::unexpected(term.error());
        }
        terms.push_back(*term);
    }
    if (terms.empty()) {
        return std::unexpected(TermError::invalid_argument);
    }
    return factory.list(std::span(terms).first(terms.size() - 1), terms.back());
}

// Successful shape checks return the original term, preserving identity and ownership for later extraction.
TermResult<Term> inspect(const Term &value, Inspect operation, std::size_t index) {
    switch (operation) {
    case Inspect::tuple_shape:
        if (value.tuple_size() == index) {
            return value;
        }
        return std::unexpected(TermError::wrong_type);
    case Inspect::tuple_element:
        return value.tuple_element(index);
    case Inspect::cons_shape:
        if (value.is_cons()) {
            return value;
        }
        return std::unexpected(TermError::wrong_type);
    case Inspect::cons_head:
        return value.head();
    case Inspect::cons_tail:
        return value.tail();
    }
    return std::unexpected(TermError::invalid_argument);
}

// Establish the checked invocation/output contract before any borrowed array or term is inspected.
bool ready(ProcessContext &context, Word *output) {
    auto &state = context.generated_calls();
    if (!state.active() || state.failure()) {
        return false;
    }
    if (!output) {
        state.fail_service(abi::v1::Status::invalid_argument);
        return false;
    }
    return true;
}
} // namespace

// Keep vector/factory allocation and unexpected native exceptions inside the generated service boundary.
std::uint8_t construct_service(ProcessContext &context, std::uint8_t operation, const Word *values, std::size_t count,
                               Word *output) noexcept {
    if (!ready(context, output)) {
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    if (operation > static_cast<std::uint8_t>(Construct::reverse) || (count != 0 && !values)) {
        context.generated_calls().fail_service(abi::v1::Status::invalid_argument);
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    try {
        return publish_result(context, construct(context, static_cast<Construct>(operation), {values, count}), output);
    } catch (const std::bad_alloc &) {
        context.generated_calls().fail_service(abi::v1::Status::out_of_memory);
    } catch (const std::length_error &) {
        context.generated_calls().fail_service(abi::v1::Status::resource_limit);
    } catch (...) {
        context.generated_calls().fail_service(abi::v1::Status::internal_error);
    }
    return static_cast<std::uint8_t>(Outcome::failure);
}

// Inspection neither allocates nor dereferences a candidate before contextual admission proves ownership.
std::uint8_t inspect_service(ProcessContext &context, InspectionRequest request, Word *output) noexcept {
    if (!ready(context, output)) {
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    if (request.operation > Inspect::cons_tail) {
        context.generated_calls().fail_service(abi::v1::Status::invalid_argument);
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    const auto value = Term::from_word(request.value, context);
    if (!value) {
        context.generated_calls().fail_service(term_status(value.error()));
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    return publish_result(context, inspect(*value, request.operation, request.index), output);
}
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_construct_v1(void *context, std::uint8_t operation, const clause::abi::v1::TermWord *values,
                                 std::size_t count, clause::abi::v1::TermWord *output) noexcept {
    using namespace clause;
    return context ? runtime::detail::construct_service(*static_cast<runtime::ProcessContext *>(context), operation,
                                                        values, count, output)
                   : static_cast<std::uint8_t>(abi::v1::ValueOutcome::failure);
}

std::uint8_t CLAUSE_inspect_v1(void *context, std::uint8_t operation, clause::abi::v1::TermWord value,
                               std::size_t index, clause::abi::v1::TermWord *output) noexcept {
    using namespace clause;
    return context
               ? runtime::detail::inspect_service(*static_cast<runtime::ProcessContext *>(context),
                                                  {static_cast<abi::v1::ContainerInspection>(operation), value, index},
                                                  output)
               : static_cast<std::uint8_t>(abi::v1::ValueOutcome::failure);
}
