#include "../typed.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <vector>

// The process dictionary builtins (plan step 41C, docs/processes.md#process-dictionary): put/2, get/0,1,
// get_keys/0,1 and erase/0,1. Entries are words of the caller's heap, found by exact equality; the order of get/0
// and get_keys/0,1 is unspecified, as in OTP.
namespace clause::runtime::builtins {
namespace {
using Entries = std::vector<ProcessDictionary::Entry>;

// Whether two words of one process are exactly equal; distinct immediates never are.
bool same(ProcessContext &context, Word left, Word right) {
    if (left == right) {
        return true;
    }
    const auto immediate = [](Word word) {
        const auto kind = TermTag{word}.get_kind();
        return kind != TermKind::list && kind != TermKind::boxed;
    };
    if (immediate(left) || immediate(right)) {
        return false;
    }
    const auto term = need(Term::from_word(left, context));
    return need(term.exactly_equal(need(Term::from_word(right, context))));
}

// The entry of `key`, or the end of the entries.
Entries::iterator find(ProcessContext &context, Entries &entries, const Term &key) {
    return std::ranges::find_if(entries, [&](const auto &entry) { return same(context, entry.key, key.word()); });
}

// The atom undefined, which reports an absent key.
TermResult<Term> undefined(ProcessContext &context) { return TermFactory(context).atom("undefined"); }

// The term of a dictionary word.
TermResult<Term> term(ProcessContext &context, Word word) { return Term::from_word(word, context); }

// A list of {Key, Value} tuples of the entries.
TermResult<Term> pairs(ProcessContext &context, const Entries &entries) {
    TermFactory factory(context);
    std::vector<Word> items;
    items.reserve(entries.size());
    for (const auto &entry : entries) {
        const std::array words{entry.key, entry.value};
        items.push_back(need(factory.tuple_words(words)).word());
    }
    return factory.list_words(items, need(factory.nil()));
}

// put(Key, Value): the previous value, or undefined.
TermResult<Term> put(ProcessContext &context, const Term &key, const Term &value) {
    auto &entries = context.dictionary().entries();
    const auto found = find(context, entries, key);
    if (found == entries.end()) {
        entries.push_back({key.word(), value.word()});
        return undefined(context);
    }
    const auto previous = found->value;
    found->value = value.word();
    return term(context, previous);
}

// get(Key): the value, or undefined.
TermResult<Term> get(ProcessContext &context, const Term &key) {
    auto &entries = context.dictionary().entries();
    const auto found = find(context, entries, key);
    return found == entries.end() ? undefined(context) : term(context, found->value);
}

// get(): every {Key, Value}.
TermResult<Term> get_all(ProcessContext &context) { return pairs(context, context.dictionary().entries()); }

// get_keys(): every key.
TermResult<Term> get_keys(ProcessContext &context) {
    const auto &entries = context.dictionary().entries();
    std::vector<Word> keys;
    keys.reserve(entries.size());
    std::ranges::transform(entries, std::back_inserter(keys), &ProcessDictionary::Entry::key);
    TermFactory factory(context);
    return factory.list_words(keys, need(factory.nil()));
}

// get_keys(Value): the keys whose value is exactly Value.
TermResult<Term> get_keys_of(ProcessContext &context, const Term &value) {
    std::vector<Word> keys;
    for (const auto &entry : context.dictionary().entries()) {
        if (same(context, entry.value, value.word())) {
            keys.push_back(entry.key);
        }
    }
    TermFactory factory(context);
    return factory.list_words(keys, need(factory.nil()));
}

// erase(Key): the value it had, or undefined.
TermResult<Term> erase(ProcessContext &context, const Term &key) {
    auto &entries = context.dictionary().entries();
    const auto found = find(context, entries, key);
    if (found == entries.end()) {
        return undefined(context);
    }
    const auto previous = found->value;
    *found = entries.back();
    entries.pop_back();
    return term(context, previous);
}

// erase(): every {Key, Value}, leaving the dictionary empty.
TermResult<Term> erase_all(ProcessContext &context) {
    auto &entries = context.dictionary().entries();
    auto result = pairs(context, entries);
    if (result) {
        entries.clear();
    }
    return result;
}

constexpr std::array DICTIONARY_BUILTINS{
    typed_entry<put>("erlang", "put"),
    typed_entry<get>("erlang", "get"),
    typed_entry<get_all>("erlang", "get"),
    typed_entry<get_keys>("erlang", "get_keys"),
    typed_entry<get_keys_of>("erlang", "get_keys"),
    typed_entry<erase>("erlang", "erase"),
    typed_entry<erase_all>("erlang", "erase"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> dictionary_builtins() noexcept { return builtins::DICTIONARY_BUILTINS; }
} // namespace clause::runtime
