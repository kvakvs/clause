#include "../../memory/heap_object.hpp"
#include "../../terms/term_layout.hpp"
#include "../portions.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <boost/container/small_vector.hpp>
#include <clause/abi/equality.hpp>
#include <vector>

// The tuple builtins of the bridge without an inline service (docs/builtins.md): setelement/3, make_tuple/2,3,
// tuple_to_list/1 and list_to_tuple/1, with OTP's badarg rules. As in OTP they run to completion: their work is
// bounded by MAX_TUPLE_ARITY (docs/builtins.md#portions).
namespace clause::runtime::builtins {
namespace {
// A tuple size argument of make_tuple: a small integer in 0..MAX_TUPLE_ARITY.
std::size_t arity(std::int64_t size) {
    if (size < 0 || static_cast<std::uint64_t>(size) > MAX_TUPLE_ARITY) {
        bad_argument();
    }
    return static_cast<std::size_t>(size);
}

// setelement(Index, Tuple, Value): a copy of Tuple with element Index (1-based) replaced.
TermResult<Term> setelement(ProcessContext &context, std::int64_t index, TupleArgument tuple, const Term &value) {
    if (index < 1 || static_cast<std::uint64_t>(index) > tuple.elements.size()) {
        bad_argument();
    }
    tuple.elements[static_cast<std::size_t>(index - 1)] = value;
    return TermFactory(context).tuple(tuple.elements);
}

// make_tuple(Size, Value): Size copies of Value.
TermResult<Term> make_tuple(ProcessContext &context, std::int64_t size, const Term &value) {
    const std::vector<Word> words(arity(size), value.word());
    return TermFactory(context).tuple_words(words);
}

// One {Index, Value} entry of make_tuple/3's list: true after storing Value at Index in `words`.
bool place(const Term &entry, std::vector<Word> &words) {
    if (!entry.is_tuple() || entry.tuple_size().value_or(0) != 2) {
        return false;
    }
    const auto index = entry.tuple_element(0);
    const auto value = entry.tuple_element(1);
    const auto position = index ? small(index->word()) : std::nullopt;
    if (!value || !position || *position < 1 || static_cast<std::uint64_t>(*position) > words.size()) {
        return false;
    }
    words[static_cast<std::size_t>(*position - 1)] = value->word();
    return true;
}

// make_tuple(Size, Default, [{Index, Value}]): later entries replace earlier ones.
TermResult<Term> make_tuple_list(ProcessContext &context, std::int64_t size, const Term &value,
                                 const ListArgument &entries) {
    std::vector<Word> words(arity(size), value.word());
    if (!std::ranges::all_of(entries.elements, [&](const Term &entry) { return place(entry, words); })) {
        bad_argument();
    }
    return TermFactory(context).tuple_words(words);
}

// tuple_to_list(Tuple): the elements read from the tuple's cell as words (word-level conversions).
Word tuple_to_list(ProcessContext &context, Arguments arguments) {
    if (arguments[0] == abi::v1::empty_tuple) {
        return abi::v1::empty_list;
    }
    // An argument is a validated term of this process, so its cell is read in place (word-level builtin arguments).
    const auto *cell = (arguments[0] & abi::v1::primary_mask) == abi::v1::boxed_tag
                           ? detail::TermAccess::trusted(arguments[0])
                           : nullptr;
    if (!cell || detail::layout::BoxHeader::kind(cell[0]) != BoxedKind::tuple) {
        bad_argument();
    }
    const std::span elements(cell + 1, detail::layout::BoxHeader::count(cell[0]));
    if (const auto bumped = detail::TermAccess::bump_list(context.heap(), elements, abi::v1::empty_list)) {
        return *bumped;
    }
    TermFactory factory(context);
    return need(factory.list_of_words(elements, need(factory.nil()))).word();
}

// list_to_tuple(List): a proper list of at most MAX_TUPLE_ARITY elements, read from its cells as words (word-level
// conversions).
Word list_to_tuple(ProcessContext &context, Arguments arguments) {
    auto list = term_of(context, arguments[0]);
    boost::container::small_vector<Word, 64> words;
    walk(list, MAX_TUPLE_ARITY + 1, [&](Word head) { words.push_back(head); });
    if (!list.is_nil() || words.size() > MAX_TUPLE_ARITY) {
        bad_argument();
    }
    return need(TermFactory(context).tuple_of_words(words)).word();
}

constexpr std::array TERM_ACCESS_BUILTINS{
    typed_entry<setelement>("erlang", "setelement"),
    typed_entry<make_tuple>("erlang", "make_tuple"),
    typed_entry<make_tuple_list>("erlang", "make_tuple"),
    BuiltinEntry{"erlang", "tuple_to_list", 1, &guarded<tuple_to_list>},
    BuiltinEntry{"erlang", "list_to_tuple", 1, &guarded<list_to_tuple>},
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> term_access_builtins() noexcept { return builtins::TERM_ACCESS_BUILTINS; }
} // namespace clause::runtime
