#include "../../memory/heap_object.hpp"
#include "../../terms/bitstrings.hpp"
#include "../../terms/term_layout.hpp"
#include "../portions.hpp"
#include "binary_search.hpp"
#include <algorithm>
#include <array>
#include <terms.hpp>

// The binary module's builtins over parts of binaries (plan step 41W, docs/builtins.md#binary-module): part/2,3,
// at/2, first/1, last/1, bin_to_list/1,2,3, copy/1,2, longest_common_prefix/1, longest_common_suffix/1,
// referenced_byte_size/1 and join/2.
namespace clause::runtime::builtins::binary {
namespace {
// A {Start, Length} tuple argument.
Range part_tuple(const TupleArgument &range, std::size_t size) {
    if (range.elements.size() != 2) {
        bad_argument();
    }
    const auto start = small(range.elements[0].word());
    const auto length = small(range.elements[1].word());
    if (!start || !length) {
        bad_argument();
    }
    return part(*start, *length, size);
}

// The bytes of a binary argument, read in place (word-level builtin arguments); badarg for any other term.
std::span<const std::byte> subject_bytes(Word word) {
    const auto bytes = detail::trusted_binary_bytes(word);
    if (!bytes) {
        bad_argument();
    }
    return *bytes;
}

// The part of a binary argument from its start and length words; badarg for anything but small integers in range.
Word part_of(ProcessContext &context, Word subject, std::span<const Word, 2> range_words) {
    const auto bytes = subject_bytes(subject);
    const auto from = small(range_words[0]);
    const auto count = small(range_words[1]);
    if (!from || !count) {
        bad_argument();
    }
    const auto range = part(*from, *count, bytes.size());
    if (const auto claimed = detail::claim_binary(context.heap(), bytes.subspan(range.start, range.size))) {
        return *claimed;
    }
    return need(slice(context, bytes, range)).word();
}

// part(Subject, Start, Length).
Word part3(ProcessContext &context, Arguments arguments) {
    return part_of(context, arguments[0], arguments.subspan<1, 2>());
}

// part(Subject, {Start, Length}): the tuple's cell read in place.
Word part2(ProcessContext &context, Arguments arguments) {
    const auto *cell = (arguments[1] & abi::v1::primary_mask) == abi::v1::boxed_tag
                           ? detail::TermAccess::trusted(arguments[1])
                           : nullptr;
    if (!cell || cell[0] != detail::layout::BoxHeader::make(BoxedKind::tuple, 2)) {
        bad_argument();
    }
    return part_of(context, arguments[0], std::span<const Word, 2>(cell + 1, 2));
}

// The byte at `position` as an integer; badarg outside the subject.
TermResult<Term> byte_at(ProcessContext &context, const Bytes &bytes, std::int64_t position) {
    if (position < 0 || static_cast<std::uint64_t>(position) >= bytes.size()) {
        bad_argument();
    }
    return TermFactory(context).integer(std::to_integer<std::int64_t>(bytes[static_cast<std::size_t>(position)]));
}

// at(Subject, Pos).
TermResult<Term> at(ProcessContext &context, const BinaryArgument &subject, std::int64_t position) {
    return byte_at(context, subject.bytes, position);
}

// first(Subject) and last(Subject): badarg for an empty subject.
TermResult<Term> first(ProcessContext &context, const BinaryArgument &subject) {
    return byte_at(context, subject.bytes, 0);
}

TermResult<Term> last(ProcessContext &context, const BinaryArgument &subject) {
    return byte_at(context, subject.bytes, static_cast<std::int64_t>(subject.bytes.size()) - 1);
}

// The bytes of `range` as a list of integers.
TermResult<Term> byte_list(ProcessContext &context, const Bytes &bytes, Range range) {
    TermFactory factory(context);
    std::vector<Word> words;
    for (std::size_t at = range.start; at < range.start + range.size; ++at) {
        words.push_back(need(factory.integer(std::to_integer<std::int64_t>(bytes[at]))).word());
    }
    return factory.list_words(words, need(factory.nil()));
}

// bin_to_list(Subject), bin_to_list(Subject, {Start, Length}) and bin_to_list(Subject, Start, Length).
TermResult<Term> bin_to_list1(ProcessContext &context, const BinaryArgument &subject) {
    return byte_list(context, subject.bytes, {.start = 0, .size = subject.bytes.size()});
}

TermResult<Term> bin_to_list2(ProcessContext &context, const BinaryArgument &subject, const TupleArgument &range) {
    return byte_list(context, subject.bytes, part_tuple(range, subject.bytes.size()));
}

TermResult<Term> bin_to_list3(ProcessContext &context, const BinaryArgument &subject, std::int64_t start,
                              std::int64_t length) {
    return byte_list(context, subject.bytes, part(start, length, subject.bytes.size()));
}

// copy(Subject, N): N copies of Subject; copy(Subject) is one copy.
TermResult<Term> copy2(ProcessContext &context, const BinaryArgument &subject, std::int64_t count) {
    if (count < 0) {
        bad_argument();
    }
    Bytes bytes;
    for (std::int64_t copy = 0; copy < count; ++copy) {
        bytes.insert(bytes.end(), subject.bytes.begin(), subject.bytes.end());
    }
    return TermFactory(context).binary(bytes);
}

TermResult<Term> copy1(ProcessContext &context, const BinaryArgument &subject) { return copy2(context, subject, 1); }

// The bytes of each element of a non-empty list of binaries; badarg for anything else.
std::vector<Bytes> binaries(const ListArgument &list) {
    std::vector<Bytes> result;
    for (const auto &element : list.elements) {
        if (!element.is_binary()) {
            bad_argument();
        }
        result.push_back(need(element.binary_bytes()));
    }
    return result;
}

// The length of the longest common prefix of `items`, or of their longest common suffix when `from_back`.
std::size_t common_length(std::vector<Bytes> items, bool from_back) {
    if (items.empty()) {
        bad_argument();
    }
    if (from_back) {
        std::ranges::for_each(items, [](Bytes &item) { std::ranges::reverse(item); });
    }
    std::size_t length = items.front().size();
    for (const auto &item : items) {
        const auto common = std::ranges::mismatch(items.front(), item).in1 - items.front().begin();
        length = std::min(length, static_cast<std::size_t>(common));
    }
    return length;
}

// longest_common_prefix(Binaries) and longest_common_suffix(Binaries).
TermResult<Term> longest_common_prefix(ProcessContext &context, const ListArgument &list) {
    return TermFactory(context).integer(static_cast<std::int64_t>(common_length(binaries(list), false)));
}

TermResult<Term> longest_common_suffix(ProcessContext &context, const ListArgument &list) {
    return TermFactory(context).integer(static_cast<std::int64_t>(common_length(binaries(list), true)));
}

// referenced_byte_size(Binary): the binary's own size, since Clause's sub-binaries are copies.
TermResult<Term> referenced_byte_size(ProcessContext &context, const BinaryArgument &subject) {
    return TermFactory(context).integer(static_cast<std::int64_t>(subject.bytes.size()));
}

// join(Binaries, Separator).
TermResult<Term> join(ProcessContext &context, const ListArgument &list, const BinaryArgument &separator) {
    Bytes joined;
    const auto items = binaries(list);
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index != 0) {
            joined.insert(joined.end(), separator.bytes.begin(), separator.bytes.end());
        }
        joined.insert(joined.end(), items[index].begin(), items[index].end());
    }
    return TermFactory(context).binary(joined);
}

constexpr std::array PART_BUILTINS{
    BuiltinEntry{"binary", "part", 2, &guarded<part2>},
    BuiltinEntry{"binary", "part", 3, &guarded<part3>},
    typed_entry<at>("binary", "at"),
    typed_entry<first>("binary", "first"),
    typed_entry<last>("binary", "last"),
    typed_entry<bin_to_list1>("binary", "bin_to_list"),
    typed_entry<bin_to_list2>("binary", "bin_to_list"),
    typed_entry<bin_to_list3>("binary", "bin_to_list"),
    typed_entry<copy1>("binary", "copy"),
    typed_entry<copy2>("binary", "copy"),
    typed_entry<longest_common_prefix>("binary", "longest_common_prefix"),
    typed_entry<longest_common_suffix>("binary", "longest_common_suffix"),
    typed_entry<referenced_byte_size>("binary", "referenced_byte_size"),
    typed_entry<join>("binary", "join"),
};
} // namespace
} // namespace clause::runtime::builtins::binary

namespace clause::runtime {
std::span<const BuiltinEntry> binary_part_builtins() noexcept { return builtins::binary::PART_BUILTINS; }
} // namespace clause::runtime
