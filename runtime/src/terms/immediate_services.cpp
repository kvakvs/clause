#include "atom_table.hpp"
#include "floats.hpp"
#include "immediate_order.hpp"
#include "service_errors.hpp"
#include "structural_order.hpp"
#include "word_order.hpp"
#include <array>
#include <clause/abi/immediate_services.hpp>
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <optional>
#include <stdexcept>

namespace clause::runtime::detail {
namespace {
using Op = abi::v1::ImmediateOperation;
using Outcome = abi::v1::ValueOutcome;

struct Fault {
    // Semantic argument rejection never enters the channel; exact infrastructure status always does.
    Outcome outcome;
    abi::v1::Status status = abi::v1::Status::ok;
};

using Result = std::expected<Word, Fault>;

// Missing canonical slots indicate failed initialization, never an Erlang false result.
Result boolean(ProcessContext &context, bool value) {
    const auto term = context.atom_storage().boolean(value);
    if (!term) {
        return std::unexpected(Fault{Outcome::failure, abi::v1::Status::internal_error});
    }
    return term->word();
}

// Predicates classify admitted representations; ports do not exist, so is_port/1 is always false.
bool predicate(Op operation, const Term &value) {
    const std::array predicates{
        value.is_atom(), value.is_integer(), value.is_number(),    value.is_boolean(),  value.is_tuple(),
        value.is_list(), value.is_binary(),  value.is_bitstring(), value.is_float(),    value.is_map(),
        value.is_pid(),  value.is_port(),    value.is_reference(), value.is_function(), value.is_native_record()};
    return predicates.at(static_cast<unsigned>(operation) - static_cast<unsigned>(Op::is_atom));
}

// Exact comparisons share Term::exactly_equal with pattern matching; numeric extensions stay explicit.
Result comparison(ProcessContext &context, Op operation, const Term &left, const Term &right) {
    if (operation <= Op::not_equal) {
        const auto result = structural_order(left, right, operation <= Op::exact_not_equal).transform([](int order) {
            return order == 0;
        });
        if (!result) {
            return std::unexpected(Fault{Outcome::failure, term_status(result.error())});
        }
        const bool inverse = operation == Op::exact_not_equal || operation == Op::not_equal;
        return boolean(context, *result != inverse);
    }
    const auto order = immediate_order(left, right);
    if (!order) {
        return std::unexpected(Fault{Outcome::failure, term_status(order.error())});
    }
    const std::array results{*order < 0, *order <= 0, *order > 0, *order >= 0};
    return boolean(context, results.at(static_cast<unsigned>(operation) - static_cast<unsigned>(Op::less)));
}

// Structural argument errors reject guards; an integer beyond the size limit is error:system_limit (rejection in a
// guard); resource and ownership errors remain infrastructure failures.
Result checked(TermResult<Word> result) {
    if (result) {
        return *result;
    }
    const auto error = result.error();
    if (error == TermError::system_limit) {
        return std::unexpected(Fault{Outcome::system_limit});
    }
    if (error == TermError::wrong_type || error == TermError::out_of_range || error == TermError::improper_list ||
        error == TermError::invalid_argument) {
        return std::unexpected(Fault{Outcome::bad_argument});
    }
    return std::unexpected(Fault{Outcome::failure, term_status(error)});
}

// Erlang element indices are positive and one-based; host access uses zero-based checked indices.
TermResult<std::size_t> element_index(const Term &index) {
    const auto number = integer_read(index);
    if (!number) {
        return std::unexpected(number.error());
    }
    if (*number <= 0 || *number > std::numeric_limits<std::size_t>::max()) {
        return std::unexpected(TermError::wrong_type);
    }
    return number->convert_to<std::size_t>() - 1;
}

// Share size and field services with host accessors rather than teaching generated code object layouts.
Result container_query(const Term &left, Op operation, const Term &right) {
    const auto word = [](const Term &value) { return value.word(); };
    const auto count = [](std::size_t value) { return encode_integer(static_cast<std::int64_t>(value)); };
    switch (operation) {
    case Op::hd:
        return checked(left.head().transform(word));
    case Op::tl:
        return checked(left.tail().transform(word));
    case Op::element:
        return checked(element_index(left)
                           .and_then([&right](std::size_t index) { return right.tuple_element(index); })
                           .transform(word));
    case Op::length:
        return checked(left.list_length().and_then(count));
    case Op::tuple_size:
        return checked(left.tuple_size().and_then(count));
    case Op::size:
        return checked(
            (left.is_bitstring() ? TermResult<std::size_t>{left.bit_size_unchecked() / 8} : left.tuple_size())
                .and_then(count));
    default:
        return std::unexpected(Fault{Outcome::bad_argument});
    }
}

// is_function/2 validates its arity argument first; the result word is nonzero when the arity matches.
Result function_arity(const Term &value, const TermResult<Integer> &arity) {
    if (!arity) {
        return checked(std::unexpected(arity.error()));
    }
    if (*arity < 0) {
        return std::unexpected(Fault{Outcome::bad_argument});
    }
    // No fun takes more than 255 arguments; a larger arity never matches.
    return Word{*arity <= 255 && value.is_function(static_cast<std::size_t>(*arity)) ? 1U : 0U};
}

// Size queries and is_function/2; other queries read containers.
Result query(const Term &left, Op operation, const Term &right) {
    if (operation == Op::bit_size || operation == Op::byte_size) {
        return checked(left.bit_size().and_then([&](std::size_t bits) {
            return encode_integer(static_cast<std::int64_t>(operation == Op::bit_size ? bits : (bits + 7) / 8));
        }));
    }
    if (operation == Op::is_function_arity) {
        return function_arity(left, integer_read(right));
    }
    return container_query(left, operation, right);
}

// Min/max return an original admitted word, with spelling-based order and left selection on equal values.
Result select(Op operation, const Term &left, const Term &right) {
    const auto order = immediate_order(left, right);
    if (!order) {
        return std::unexpected(Fault{Outcome::failure, term_status(order.error())});
    }
    const bool lhs = operation == Op::minimum ? *order <= 0 : *order >= 0;
    return lhs ? left.word() : right.word();
}

// self/0, node/0 and node/1: the caller's pid, or the name of the only node (node/1 of a pid, reference or port).
Result identity(ProcessContext &context, Op operation, const Term &operand) {
    if (operation == Op::self) {
        return checked(
            TermFactory(context).pid(context.identity()).transform([](const Term &pid) { return pid.word(); }));
    }
    if (operation == Op::node_of && !operand.is_pid() && !operand.is_reference() && !operand.is_port()) {
        return std::unexpected(Fault{Outcome::bad_argument});
    }
    return checked(TermFactory(context).atom("nonode@nohost").transform([](const Term &node) { return node.word(); }));
}

// Boolean operators validate admitted terms; lazy lowering checks only the reached left operand here.
Result logical(ProcessContext &context, Op operation, const Term &left, const Term &right) {
    if (!left.is_boolean()) {
        return std::unexpected(Fault{Outcome::bad_argument});
    }
    if (operation == Op::boolean_check) {
        return left.word();
    }
    const bool lhs = left.atom_spelling_unchecked() == "true";
    if (operation == Op::logical_not) {
        return boolean(context, !lhs);
    }
    if (!right.is_boolean()) {
        return std::unexpected(Fault{Outcome::bad_argument});
    }
    const bool rhs = right.atom_spelling_unchecked() == "true";
    const std::array results{lhs && rhs, lhs || rhs, lhs != rhs};
    return boolean(context, results.at(static_cast<unsigned>(operation) - static_cast<unsigned>(Op::logical_and)));
}

// The operations between the type tests and the boolean operators: queries, min/max, self/0 and node/0,1.
Result inspect(ProcessContext &context, Op operation, const Term &left, const Term &right) {
    if (operation == Op::minimum || operation == Op::maximum) {
        return select(operation, left, right);
    }
    if (operation >= Op::self) {
        return identity(context, operation, left);
    }
    const auto result = query(left, operation, right);
    if (result && operation == Op::is_function_arity) {
        return boolean(context, *result != 0);
    }
    return result;
}

// Dispatch only semantically authorized opcodes; later representation services extend this boundary.
Result evaluate(ProcessContext &context, Op operation, const Term &left, const Term &right) {
    if (operation >= Op::bit_size) {
        return query(left, operation, right);
    }
    if (operation >= Op::add) {
        return checked(
            numeric_service(context, operation, left, right).transform([](const Term &value) { return value.word(); }));
    }
    if (operation >= Op::logical_not) {
        return logical(context, operation, left, right);
    }
    if (operation <= Op::greater_equal) {
        return comparison(context, operation, left, right);
    }
    if (operation <= Op::is_native_record) {
        return boolean(context, predicate(operation, left));
    }
    return inspect(context, operation, left, right);
}

// A unary service never tries to admit its unused placeholder operand.
bool binary(Op operation) {
    return operation <= Op::greater_equal || operation == Op::is_function_arity || operation == Op::element ||
           operation == Op::minimum || operation == Op::maximum ||
           (operation >= Op::logical_and && operation <= Op::logical_xor) ||
           (operation >= Op::add && operation < Op::positive) || operation == Op::divide;
}

// Ownership/encoding failures precede semantic argument classification and cannot reject a guard silently.
Result invoke(ProcessContext &context, Op operation, Word left, Word right) {
    const auto lhs = Term::from_word(left, context);
    const auto rhs = binary(operation) ? Term::from_word(right, context) : TermResult<Term>{Term{}};
    if (!lhs || !rhs) {
        return std::unexpected(Fault{Outcome::failure, abi::v1::Status::invalid_argument});
    }
    return evaluate(context, operation, *lhs, *rhs);
}
} // namespace

// Publish only checked results and preserve first infrastructure failure under native fault/reentry seams.
namespace {
// The order of two atom words by their spellings, read from the atom table with no Term (atom ordering); none for
// any other pair.
std::optional<int> atom_order(Word left, Word right) noexcept {
    constexpr Word mask = (Word{1} << abi::v1::atom_tag_bits) - 1;
    if ((left & mask) != abi::v1::atom_tag || (right & mask) != abi::v1::atom_tag) {
        return std::nullopt;
    }
    if (left == right) {
        return 0;
    }
    const auto a = detail::atom_table::spelling(left);
    const auto b = a ? detail::atom_table::spelling(right) : std::nullopt;
    if (!b) {
        return std::nullopt;
    }
    // Distinct atoms have distinct spellings.
    return *a < *b ? -1 : 1;
}

// ==, /=, <, =<, >, >= of two words atom_order or word_order decides, with no Term admission (cheap comparisons).
std::optional<Word> word_comparison(ProcessContext &context, Op operation, Word left, Word right) noexcept {
    if (operation < Op::equal || operation > Op::greater_equal) {
        return std::nullopt;
    }
    auto order = atom_order(left, right);
    if (!order) {
        order = word_order(context.heap(), left, right);
    }
    if (!order) {
        return std::nullopt;
    }
    constexpr std::array holds{+[](int o) { return o == 0; }, +[](int o) { return o != 0; },
                               +[](int o) { return o < 0; },  +[](int o) { return o <= 0; },
                               +[](int o) { return o > 0; },  +[](int o) { return o >= 0; }};
    const auto truth = holds.at(static_cast<std::size_t>(operation) - static_cast<std::size_t>(Op::equal))(*order);
    return truth ? abi::v1::true_atom : abi::v1::false_atom;
}
} // namespace

std::uint8_t immediate_service(ProcessContext &context, std::uint8_t operation, Word left, Word right,
                               Word *output) noexcept {
    auto &state = context.generated_calls();
    if (!state.active() || state.failure()) {
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    if (!output || operation > static_cast<std::uint8_t>(Op::byte_size)) {
        state.fail_service(abi::v1::Status::invalid_argument);
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    if (const auto compared = word_comparison(context, static_cast<Op>(operation), left, right)) {
        *output = *compared;
        return static_cast<std::uint8_t>(Outcome::success);
    }
    try {
        const auto result = invoke(context, static_cast<Op>(operation), left, right);
        if (!result) {
            state.fail_service(result.error().status);
            return static_cast<std::uint8_t>(result.error().outcome);
        }
        *output = *result;
        return static_cast<std::uint8_t>(Outcome::success);
    } catch (const std::bad_alloc &) {
        state.fail_service(abi::v1::Status::out_of_memory);
        return static_cast<std::uint8_t>(Outcome::failure);
    } catch (const std::length_error &) {
        state.fail_service(abi::v1::Status::resource_limit);
        return static_cast<std::uint8_t>(Outcome::failure);
    } catch (...) {
        state.fail_service(abi::v1::Status::internal_error);
        return static_cast<std::uint8_t>(Outcome::failure);
    }
}
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_immediate_v1(void *context, std::uint8_t operation, clause::abi::v1::TermWord left,
                                 clause::abi::v1::TermWord right, clause::abi::v1::TermWord *output) noexcept {
    if (!context) {
        return static_cast<std::uint8_t>(clause::abi::v1::ValueOutcome::failure);
    }
    return clause::runtime::detail::immediate_service(*static_cast<clause::runtime::ProcessContext *>(context),
                                                      operation, left, right, output);
}
