#include "../memory/heap_object.hpp"
#include "records.hpp"
#include "service_errors.hpp"
#include "terms.hpp"
#include <array>
#include <clause/abi/records.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <stdexcept>
#include <vector>

// CLAUSE_record_v1: native record construction, access, update, matching and tests (docs/native-records.md).
namespace clause::runtime::detail {
namespace {
using Op = abi::v1::RecordOperation;
using Check = abi::v1::RecordCheck;
using Outcome = abi::v1::RecordOutcome;

// A semantic failure and its payload, or an infrastructure error with no payload.
struct RecordFault {
    Outcome outcome;
    TermError error = TermError::invalid_argument;
    Term payload = {};
};

// One result word per operation, except match, which yields one per field.
using RecordResult = std::expected<std::vector<Term>, RecordFault>;

// An infrastructure error that leaves output untouched.
RecordFault fault(TermError error) { return {Outcome::failure, error}; }

// Whether a record's captured identity passes the operation's check against the given module and name atoms.
bool accepted(const RecordDefinition &definition, Check check, Word module, Word name) {
    switch (check) {
    case Check::any:
        return true;
    case Check::name:
        return definition.name == name;
    case Check::module_name:
        return definition.module == module && definition.name == name;
    case Check::exported_module_name:
        return definition.exported && definition.module == module && definition.name == name;
    case Check::exported_or_module:
        return definition.exported || definition.module == module;
    }
    return false;
}

// The record of operands record, module, name, ...; anything else that fails the check is a bad record.
std::expected<RecordView, RecordFault> checked(std::span<const Term> terms, Check check) {
    if (terms.size() < 3) {
        return std::unexpected(fault(TermError::invalid_encoding));
    }
    const auto view = record_view(terms[0]);
    if (!view || !accepted(*view->definition, check, terms[1].word(), terms[2].word())) {
        if (view || view.error() == TermError::wrong_type) {
            return std::unexpected(RecordFault{Outcome::bad_record, {}, terms[0]});
        }
        return std::unexpected(fault(view.error()));
    }
    return *view;
}

// {badfield, {{Module, Name}, Field}} carries this payload for the record operand and the field at index.
RecordFault bad_field(ProcessContext &context, std::span<const Term> terms, std::size_t index) {
    const auto &field = terms[index];
    TermFactory factory(context);
    const auto identity = record_identity(terms[0]);
    if (!identity) {
        return fault(identity.error());
    }
    const auto owner = factory.tuple(std::array{identity->first, identity->second});
    const auto payload = owner.and_then([&](const Term &pair) { return factory.tuple(std::array{pair, field}); });
    return payload ? RecordFault{Outcome::bad_field, {}, *payload} : fault(payload.error());
}

// Build a record of the descriptor's registered definition from every field value.
RecordResult make(ProcessContext &context, const void *descriptor, std::span<const Term> terms) {
    const auto *definition = context.code_server().record_definition(descriptor);
    if (!definition) {
        return std::unexpected(fault(TermError::wrong_owner));
    }
    return TermFactory(context)
        .native_record(*definition, terms)
        .transform([](Term value) { return std::vector{value}; })
        .transform_error(fault);
}

// Read one field of a record that passes the check.
RecordResult get(ProcessContext &context, std::span<const Term> terms, Check check) {
    const auto view = checked(terms, check);
    if (!view || terms.size() != 4) {
        return std::unexpected(view ? fault(TermError::invalid_encoding) : view.error());
    }
    const auto position = record_position(*view->definition, terms[3].word());
    if (!position) {
        return std::unexpected(bad_field(context, terms, 3));
    }
    return TermAccess::child(terms[0], view->values[*position])
        .transform([](Term value) { return std::vector{value}; })
        .transform_error(fault);
}

// Copy a record that passes the check with each field/value pair replaced; the definition stays captured.
RecordResult update(ProcessContext &context, std::span<const Term> terms, Check check) {
    const auto view = checked(terms, check);
    if (!view || terms.size() % 2 == 0) {
        return std::unexpected(view ? fault(TermError::invalid_encoding) : view.error());
    }
    std::vector<Word> values(view->values.begin(), view->values.end());
    for (std::size_t i = 3; i < terms.size(); i += 2) {
        const auto position = record_position(*view->definition, terms[i].word());
        if (!position) {
            return std::unexpected(bad_field(context, terms, i));
        }
        values[*position] = terms[i + 1].word();
    }
    return TermFactory(context)
        .native_record_words(*view->definition, values)
        .transform([](Term value) { return std::vector{value}; })
        .transform_error(fault);
}

// Extract the listed fields of a record that passes the check; any failure is no match.
RecordResult match(std::span<const Term> terms, Check check) {
    const auto view = checked(terms, check);
    if (!view) {
        return std::unexpected(view.error().outcome == Outcome::bad_record ? RecordFault{Outcome::no_match}
                                                                           : view.error());
    }
    std::vector<Term> fields;
    fields.reserve(terms.size() - 3);
    for (const auto &field : terms.subspan(3)) {
        const auto position = record_position(*view->definition, field.word());
        if (!position) {
            return std::unexpected(RecordFault{Outcome::no_match});
        }
        auto value = TermAccess::child(terms[0], view->values[*position]);
        if (!value) {
            return std::unexpected(fault(value.error()));
        }
        fields.push_back(*value);
    }
    return fields;
}

// Dispatch one operation over admitted operands.
RecordResult evaluate(ProcessContext &context, Op operation, Check check, const void *descriptor,
                      std::span<const Term> terms) {
    switch (operation) {
    case Op::make:
        return make(context, descriptor, terms);
    case Op::get:
        return get(context, terms, check);
    case Op::update:
        return update(context, terms, check);
    case Op::match:
    case Op::test:
        return match(operation == Op::test ? terms.first(std::min<std::size_t>(terms.size(), 3)) : terms, check);
    }
    return std::unexpected(fault(TermError::invalid_encoding));
}

// Publish results or a semantic payload; infrastructure errors fail the invocation and touch no output.
Outcome publish(ProcessContext &context, const RecordResult &result, Word *output) {
    if (result) {
        for (std::size_t i = 0; i < result->size(); ++i) {
            output[i] = (*result)[i].word();
        }
        return Outcome::success;
    }
    const auto &failure = result.error();
    if (failure.outcome == Outcome::failure) {
        context.generated_calls().fail_service(term_status(failure.error));
        return Outcome::failure;
    }
    if (failure.outcome != Outcome::no_match) {
        output[0] = failure.payload.word();
    }
    return failure.outcome;
}

// Admit every operand as a term of this process.
TermResult<std::vector<Term>> admit(ProcessContext &context, std::span<const Word> values) {
    std::vector<Term> terms;
    terms.reserve(values.size());
    for (const auto word : values) {
        auto term = Term::from_word(word, context);
        if (!term) {
            return std::unexpected(term.error());
        }
        terms.push_back(*term);
    }
    return terms;
}

// Check the invocation, pointers and enumerators before reading any borrowed argument.
bool ready(ProcessContext &context, std::uint8_t operation, std::uint8_t check, const Word *values, std::size_t count,
           const Word *output) {
    auto &calls = context.generated_calls();
    if (!calls.active() || calls.failure()) {
        return false;
    }
    if (!output || (count != 0 && !values) || operation > static_cast<std::uint8_t>(Op::test) ||
        check > static_cast<std::uint8_t>(Check::exported_or_module)) {
        calls.fail_service(abi::v1::Status::invalid_argument);
        return false;
    }
    return true;
}

// Contain marshalling and construction allocations at the generated service boundary.
std::uint8_t service(ProcessContext &context, std::uint8_t operation, std::uint8_t check, const void *descriptor,
                     const Word *values, std::size_t count, Word *output) noexcept {
    if (!ready(context, operation, check, values, count, output)) {
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    try {
        const auto terms = admit(context, {values, count});
        if (!terms) {
            context.generated_calls().fail_service(term_status(terms.error()));
            return static_cast<std::uint8_t>(Outcome::failure);
        }
        const auto result =
            evaluate(context, static_cast<Op>(operation), static_cast<Check>(check), descriptor, *terms);
        return static_cast<std::uint8_t>(publish(context, result, output));
    } catch (const std::bad_alloc &) {
        context.generated_calls().fail_service(abi::v1::Status::out_of_memory);
    } catch (...) {
        context.generated_calls().fail_service(abi::v1::Status::internal_error);
    }
    return static_cast<std::uint8_t>(Outcome::failure);
}
} // namespace
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_record_v1(void *context, std::uint8_t operation, std::uint8_t check, const void *descriptor,
                              const clause::abi::v1::TermWord *values, std::size_t count,
                              clause::abi::v1::TermWord *output) noexcept {
    using namespace clause;
    if (!context) {
        return static_cast<std::uint8_t>(abi::v1::RecordOutcome::failure);
    }
    return runtime::detail::service(*static_cast<runtime::ProcessContext *>(context), operation, check, descriptor,
                                    values, count, output);
}
