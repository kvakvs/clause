#include "bitstrings.hpp"
#include "service_errors.hpp"
#include "terms.hpp"
#include <boost/container/small_vector.hpp>
#include <clause/runtime/process_context.hpp>
#include <new>
#include <stdexcept>

namespace clause::runtime::detail {
namespace {
using Op = abi::v1::BitOperation;
using Outcome = abi::v1::ValueOutcome;

// Read metadata only from canonical small integers, rejecting malformed service descriptors.
TermResult<BitSegment> segment(std::span<const Term> values) {
    const auto descriptor = values[0].integer_value();
    if (!descriptor || *descriptor < 256 || *descriptor > 65'663 || (*descriptor & 7) > 5 || (*descriptor & 128) != 0) {
        return std::unexpected(TermError::invalid_encoding);
    }
    const auto bits = static_cast<unsigned>(*descriptor);
    return BitSegment{static_cast<abi::v1::BitType>(bits & 7),
                      (bits & abi::v1::bit_little) != 0,
                      (bits & abi::v1::bit_signed) != 0,
                      (bits & abi::v1::bit_all) != 0,
                      (bits & abi::v1::bit_empty) != 0,
                      bits >> 8,
                      values[1],
                      values[2]};
}

// Stage all segments privately; only successful complete construction reserves process heap storage.
TermResult<BitExtract> make(ProcessContext &context, std::span<const Term> values) {
    if (values.size() % 3 != 0) {
        return std::unexpected(TermError::invalid_encoding);
    }
    BitWriter writer;
    for (std::size_t i = 0; i < values.size(); i += 3) {
        const auto field = segment(values.subspan(i, 3));
        if (!field) {
            return std::unexpected(field.error());
        }
        const auto added = bit_construct(writer, *field);
        if (!added) {
            return std::unexpected(added.error());
        }
    }
    return BitAccess::make(context.heap(), writer.bytes, writer.length).transform([&](Term value) {
        return BitExtract{std::move(value), writer.length};
    });
}

// Part uses byte indices and permits negative length; bounds are checked before either multiplication.
TermResult<BitExtract> part(ProcessContext &context, std::span<const Term> values) {
    if (values.size() != 3 || !values[0].is_binary()) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto start = integer_read(values[1]);
    const auto count = integer_read(values[2]);
    if (!start || !count) {
        return std::unexpected(TermError::wrong_type);
    }
    Integer offset = *start;
    Integer length = *count;
    if (length < 0) {
        offset += length;
        length = -length;
    }
    const auto bytes = values[0].bit_size().value() / 8;
    if (offset < 0 || length < 0 || offset + length > bytes) {
        return std::unexpected(TermError::invalid_argument);
    }
    return BitAccess::slice(context.heap(), values[0], offset.convert_to<std::size_t>() * 8,
                            length.convert_to<std::size_t>() * 8)
        .transform([](Term value) { return BitExtract{std::move(value), 0}; });
}

// Join a proper list of bitstrings in order into one bitstring.
TermResult<BitExtract> concat(ProcessContext &context, std::span<const Term> values) {
    if (values.size() != 1) {
        return std::unexpected(TermError::invalid_encoding);
    }
    BitWriter writer;
    auto rest = values[0];
    while (rest.is_cons()) {
        const auto head = rest.head();
        const auto next = rest.tail();
        const auto view = head.and_then(bit_view);
        if (!view || !next) {
            return std::unexpected(TermError::wrong_type);
        }
        const auto added = writer.append(*view);
        if (!added) {
            return std::unexpected(added.error());
        }
        rest = *next;
    }
    if (!rest.is_nil()) {
        return std::unexpected(TermError::wrong_type);
    }
    return BitAccess::make(context.heap(), writer.bytes, writer.length).transform([&](Term value) {
        return BitExtract{std::move(value), writer.length};
    });
}

// Validate arity before pattern services inspect the candidate or cursor.
bool pattern_arity(Op operation, std::size_t count) {
    const auto expected = operation == Op::test ? 1U : (operation == Op::finish ? 2U : 5U);
    return count == expected;
}

// Pattern services never advance a supplied cursor on truncation, invalid sizes or invalid encoding.
TermResult<BitExtract> pattern(ProcessContext &context, Op operation, std::span<const Term> values) {
    if (!pattern_arity(operation, values.size())) {
        return std::unexpected(TermError::invalid_encoding);
    }
    const auto size = values[0].bit_size();
    if (!size) {
        return std::unexpected(size.error());
    }
    if (operation == Op::test) {
        return BitExtract{values[0], 0};
    }
    const auto cursor = values[1].integer_value();
    if (!cursor || *cursor < 0 || static_cast<std::uint64_t>(*cursor) > *size) {
        return std::unexpected(TermError::invalid_argument);
    }
    if (operation == Op::finish) {
        return static_cast<std::size_t>(*cursor) == *size ? TermResult<BitExtract>{{values[0], *size}}
                                                          : std::unexpected(TermError::invalid_argument);
    }
    const auto field = segment(values.subspan(2));
    if (!field) {
        return std::unexpected(field.error());
    }
    return bit_extract(context, values[0], static_cast<std::size_t>(*cursor), *field);
}

// Construction, part queries and pattern extraction retain one checked service transport.
TermResult<BitExtract> evaluate(ProcessContext &context, Op operation, std::span<const Term> values) {
    if (operation == Op::make) {
        return make(context, values);
    }
    if (operation == Op::part) {
        return part(context, values);
    }
    if (operation == Op::concat) {
        return concat(context, values);
    }
    return pattern(context, operation, values);
}

// Admitted service arguments: a construction of up to five segments needs no allocation for them (OPT0.9).
using Terms = boost::container::small_vector<Term, 15>;

// Admission failures are infrastructure errors even in a guard or failed pattern candidate.
TermResult<Terms> admit(ProcessContext &context, std::span<const Word> words) {
    Terms terms;
    terms.reserve(words.size());
    for (const auto word : words) {
        const auto term = Term::from_word(word, context);
        if (!term) {
            return std::unexpected(term.error());
        }
        terms.push_back(*term);
    }
    return terms;
}

// Publish both checked output words together; ordinary segment errors reject guards and patterns silently. An
// extracted integer beyond the size limit does not match either (ERTS returns no value for it).
Outcome publish_result(ProcessContext &context, const TermResult<BitExtract> &result, Word *output) {
    if (result) {
        output[0] = result->value.word();
        output[1] = encode_integer(static_cast<std::int64_t>(result->cursor)).value();
        return Outcome::success;
    }
    const auto error = result.error();
    if (error == TermError::wrong_type || error == TermError::invalid_argument || error == TermError::out_of_range ||
        error == TermError::system_limit) {
        return Outcome::bad_argument;
    }
    context.generated_calls().fail_service(term_status(error));
    return Outcome::failure;
}

// Check invocation and array contracts before dereferencing a borrowed pointer.
bool ready(ProcessContext &context, std::uint8_t operation, const Word *values, std::size_t count, Word *output) {
    auto &calls = context.generated_calls();
    if (!calls.active() || calls.failure()) {
        return false;
    }
    if (!output || (count != 0 && !values) || operation > static_cast<std::uint8_t>(Op::concat)) {
        calls.fail_service(abi::v1::Status::invalid_argument);
        return false;
    }
    return true;
}
} // namespace

// Keep native allocations and unexpected failures inside the generated-call boundary.
std::uint8_t bits_service(ProcessContext &context, std::uint8_t operation, const Word *values, std::size_t count,
                          Word *output) noexcept {
    if (!ready(context, operation, values, count, output)) {
        return static_cast<std::uint8_t>(Outcome::failure);
    }
    try {
        const auto terms = admit(context, {values, count});
        if (!terms) {
            context.generated_calls().fail_service(term_status(terms.error()));
            return static_cast<std::uint8_t>(Outcome::failure);
        }
        return static_cast<std::uint8_t>(publish_result(
            context, evaluate(context, static_cast<Op>(operation), {terms->data(), terms->size()}), output));
    } catch (const std::bad_alloc &) {
        context.generated_calls().fail_service(abi::v1::Status::out_of_memory);
    } catch (const std::length_error &) {
        context.generated_calls().fail_service(abi::v1::Status::resource_limit);
    } catch (...) {
        context.generated_calls().fail_service(abi::v1::Status::internal_error);
    }
    return static_cast<std::uint8_t>(Outcome::failure);
}
} // namespace clause::runtime::detail

std::uint8_t CLAUSE_bits_v1(void *context, std::uint8_t operation, const clause::abi::v1::TermWord *values,
                            std::size_t count, clause::abi::v1::TermWord *output) noexcept {
    if (!context) {
        return static_cast<std::uint8_t>(clause::abi::v1::ValueOutcome::failure);
    }
    return clause::runtime::detail::bits_service(*static_cast<clause::runtime::ProcessContext *>(context), operation,
                                                 values, count, output);
}
