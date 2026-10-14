#include "../memory/persistent_terms.hpp"
#include "erlang/hashing.hpp"
#include "typed.hpp"
#include <array>
#include <terms.hpp>

// The persistent_term builtins (plan step 41M2, docs/builtins.md#persistent-terms): put/2, put_new/2, get/0,1,2,
// erase/1 and info/0 over the runtime-wide table (docs/runtime.md#persistent-terms). A read copies the value into
// the caller's heap; a missing key of get/1 and a different value for put_new/2 are badarg, as in OTP.
namespace clause::runtime::builtins {
namespace {
using detail::MessageBuffer;
using detail::PersistentTerms;

// The default of get/2, returned when the key is not stored.
struct Default {
    // The argument as passed.
    Term term;
};
} // namespace

// Any term as get/2's default.
template <> struct Argument<Default> {
    static TermResult<std::optional<Default>> convert(const Term &term) { return Default{term}; }
};

namespace {

// A matcher of stored keys exactly equal to `key`, a term of `context`.
PersistentTerms::SameKey same_key(ProcessContext &context, const Term &key) {
    return
        [&context, key](const MessageBuffer &stored) { return need(need(stored.value(context)).exactly_equal(key)); };
}

// The stored value of `key`, if any.
std::optional<PersistentTerms::Buffer> lookup(ProcessContext &context, const Term &key) {
    return context.persistent_terms().find(term_hash(context, key), same_key(context, key));
}

// Store `value` under `key`.
void store(ProcessContext &context, const Term &key, const Term &value) {
    static_cast<void>(context.persistent_terms().store(
        term_hash(context, key),
        {.key = need(MessageBuffer::copy_of(key)), .value = need(MessageBuffer::copy_of(value))},
        same_key(context, key)));
}

// put(Key, Value).
TermResult<Term> put(ProcessContext &context, const Term &key, const Term &value) {
    store(context, key, value);
    return TermFactory(context).atom("ok");
}

// put_new(Key, Value): store Value unless Key is stored; ok also when it is stored with an exactly equal value.
TermResult<Term> put_new(ProcessContext &context, const Term &key, const Term &value) {
    if (const auto stored = lookup(context, key)) {
        if (!need(need((*stored)->value(context)).exactly_equal(value))) {
            bad_argument();
        }
    } else {
        store(context, key, value);
    }
    return TermFactory(context).atom("ok");
}

// get(Key): the stored value; badarg when Key is not stored.
TermResult<Term> get1(ProcessContext &context, const Term &key) {
    const auto stored = lookup(context, key);
    if (!stored) {
        bad_argument();
    }
    return (*stored)->copy_to(context);
}

// get(Key, Default).
TermResult<Term> get2(ProcessContext &context, const Term &key, const Default &fallback) {
    const auto stored = lookup(context, key);
    return stored ? (*stored)->copy_to(context) : fallback.term;
}

// get(): every stored association as {Key, Value}.
TermResult<Term> get0(ProcessContext &context) {
    TermFactory factory(context);
    std::vector<Term> items;
    for (const auto &entry : context.persistent_terms().entries()) {
        const std::array pair{need(entry.key->copy_to(context)), need(entry.value->copy_to(context))};
        items.push_back(need(factory.tuple(pair)));
    }
    return factory.list(items);
}

// erase(Key): whether Key was stored.
TermResult<Term> erase(ProcessContext &context, const Term &key) {
    return TermFactory(context).boolean(
        context.persistent_terms().erase(term_hash(context, key), same_key(context, key)));
}

// info(): #{count => Entries, memory => Bytes}.
TermResult<Term> info(ProcessContext &context) {
    TermFactory factory(context);
    const auto &table = context.persistent_terms();
    const std::array entries{
        std::pair{need(factory.atom("count")), need(factory.integer(static_cast<std::int64_t>(table.count())))},
        std::pair{need(factory.atom("memory")),
                  need(factory.integer(static_cast<std::int64_t>(table.words() * sizeof(Word))))}};
    return factory.map(entries);
}

constexpr std::array PERSISTENT_TERM_BUILTINS{
    typed_entry<put>("persistent_term", "put"),   typed_entry<put_new>("persistent_term", "put_new"),
    typed_entry<get0>("persistent_term", "get"),  typed_entry<get1>("persistent_term", "get"),
    typed_entry<get2>("persistent_term", "get"),  typed_entry<erase>("persistent_term", "erase"),
    typed_entry<info>("persistent_term", "info"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> persistent_term_builtins() noexcept { return builtins::PERSISTENT_TERM_BUILTINS; }
} // namespace clause::runtime
