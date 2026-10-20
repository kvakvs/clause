#include "bitstrings.hpp"
#include "service_errors.hpp"
#include "term_layout.hpp"
#include "terms.hpp"
#include <array>
#include <boost/container/small_vector.hpp>
#include <clause/abi/bits.hpp>
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

// Construct the segments of `values` (three words each) into a private writer.
TermResult<void> construct_all(BitWriter &writer, std::span<const Term> values) {
    for (std::size_t i = 0; i < values.size(); i += 3) {
        const auto field = segment(values.subspan(i, 3));
        if (!field) {
            return std::unexpected(field.error());
        }
        if (const auto added = bit_construct(writer, *field); !added) {
            return std::unexpected(added.error());
        }
    }
    return {};
}

// Whether a construction starts with a whole binary, as <<Acc/binary, ...>> does (OPT0.6).
bool accumulates(std::span<const Term> values) {
    const auto first = values.size() > 3 ? segment(values.first(3)) : std::unexpected(TermError::invalid_encoding);
    return first && first->type == abi::v1::BitType::binary && first->all;
}

TermResult<BitExtract> extract(TermResult<Term> value) {
    return value.transform([](Term made) {
        const auto bits = made.bit_size().value_or(0);
        return BitExtract{made, bits};
    });
}

// The decoded descriptor of a segment's argument words; none when it is no valid small descriptor.
std::optional<unsigned> descriptor_of(Word word) {
    const auto descriptor = decode_integer(word);
    if (!descriptor || *descriptor < 256 || *descriptor > 65'663 || (*descriptor & 7) > 5 || (*descriptor & 128) != 0) {
        return std::nullopt;
    }
    return static_cast<unsigned>(*descriptor);
}

// An integer segment of small size and value whose width is at most a word, read from its three argument words;
// none for any other segment (word constructions).
std::optional<std::array<std::uint64_t, 2>> word_argument(std::span<const Word> triple, bool &little) {
    const auto descriptor = descriptor_of(triple[0]);
    const auto size = decode_integer(triple[1]);
    const auto value = decode_integer(triple[2]);
    if (!descriptor || !size || !value || static_cast<abi::v1::BitType>(*descriptor & 7) != abi::v1::BitType::integer ||
        (*descriptor & (abi::v1::bit_all | abi::v1::bit_empty)) != 0) {
        return std::nullopt;
    }
    const auto unit = *descriptor >> 8;
    if (*size < 0 || static_cast<std::uint64_t>(*size) > 64 / unit) {
        return std::nullopt;
    }
    little = (*descriptor & abi::v1::bit_little) != 0;
    return std::array{static_cast<std::uint64_t>(*value), static_cast<std::uint64_t>(*size) * unit};
}

// Whether a segment's argument words are a whole binary, as the accumulator of <<Acc/binary, ...>> is.
bool accumulator_words(std::span<const Word> values) {
    const auto descriptor = values.size() > 3 ? descriptor_of(values[0]) : std::nullopt;
    return descriptor && static_cast<abi::v1::BitType>(*descriptor & 7) == abi::v1::BitType::binary &&
           (*descriptor & abi::v1::bit_all) != 0;
}

// Write segments given as argument words into `writer`; none when one is no word-sized integer segment.
std::optional<TermResult<void>> word_segments(BitWriter &writer, std::span<const Word> values) {
    for (std::size_t i = 0; i < values.size(); i += 3) {
        bool little = false;
        const auto word = word_argument(values.subspan(i, 3), little);
        if (!word) {
            return std::nullopt;
        }
        if (const auto added = bit_word_construct(writer, (*word)[0], (*word)[1], little); !added) {
            return added;
        }
    }
    return TermResult<void>{};
}

// A construction of word-sized integer segments, optionally after an appendable accumulator, built from the
// argument words with no Term per argument (word constructions); none when the general path must build it.
std::optional<TermResult<BitExtract>> word_make(ProcessContext &context, std::span<const Word> values) {
    if (values.empty() || values.size() % 3 != 0) {
        return std::nullopt;
    }
    const bool accumulator = accumulator_words(values);
    BitWriter writer;
    const auto written = word_segments(writer, values.subspan(accumulator ? 3 : 0));
    if (!written) {
        return std::nullopt;
    }
    if (!*written) {
        return std::unexpected(written->error());
    }
    if (!accumulator) {
        return BitAccess::make(context.heap(), writer.bytes, writer.length).transform([&](Term value) {
            return BitExtract{value, writer.length};
        });
    }
    const auto start = Term::from_word(values[2], context);
    auto appended = start ? BitAccess::append(context.heap(), *start, writer) : std::nullopt;
    return appended ? std::optional{extract(std::move(*appended))} : std::nullopt;
}

// Stage all segments privately; only successful complete construction reserves process heap storage. A
// construction appending to an accumulator writes in place when it can, and otherwise leaves room to (OPT0.6).
TermResult<BitExtract> make(ProcessContext &context, std::span<const Term> values) {
    if (values.size() % 3 != 0) {
        return std::unexpected(TermError::invalid_encoding);
    }
    if (accumulates(values)) {
        BitWriter rest;
        if (const auto built = construct_all(rest, values.subspan(3)); !built) {
            return std::unexpected(built.error());
        }
        if (auto appended = BitAccess::append(context.heap(), values[2], rest)) {
            return extract(std::move(*appended));
        }
    }
    BitWriter writer;
    if (const auto built = construct_all(writer, values); !built) {
        return std::unexpected(built.error());
    }
    return BitAccess::make(context.heap(), writer.bytes, writer.length).transform([&](Term value) {
        return BitExtract{value, writer.length};
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
    const auto bytes = values[0].bit_size_unchecked() / 8;
    if (offset < 0 || length < 0 || offset + length > bytes) {
        return std::unexpected(TermError::invalid_argument);
    }
    return BitAccess::slice(context.heap(), values[0], offset.convert_to<std::size_t>() * 8,
                            length.convert_to<std::size_t>() * 8)
        .transform([](Term value) { return BitExtract{value, 0}; });
}

// Join a proper list of bitstrings in order into one bitstring.
TermResult<BitExtract> concat(ProcessContext &context, std::span<const Term> values) {
    if (values.size() != 1) {
        return std::unexpected(TermError::invalid_encoding);
    }
    BitWriter writer;
    auto rest = values[0];
    while (rest.is_cons()) {
        const auto head = rest.head_unchecked();
        const auto next = rest.tail_unchecked();
        const auto view = bit_view(head);
        if (!view) {
            return std::unexpected(TermError::wrong_type);
        }
        const auto added = writer.append(*view);
        if (!added) {
            return std::unexpected(added.error());
        }
        rest = next;
    }
    if (!rest.is_nil()) {
        return std::unexpected(TermError::wrong_type);
    }
    return BitAccess::make(context.heap(), writer.bytes, writer.length).transform([&](Term value) {
        return BitExtract{value, writer.length};
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
        if (operation == static_cast<std::uint8_t>(Op::make)) {
            if (const auto made = word_make(context, {values, count})) {
                return static_cast<std::uint8_t>(publish_result(context, *made, output));
            }
        }
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
