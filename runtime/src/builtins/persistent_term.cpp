#include "../memory/heap_object.hpp"
#include "../memory/persistent_terms.hpp"
#include "../scheduler/executor.hpp"
#include "erlang/hashing.hpp"
#include "typed.hpp"
#include <array>
#include <terms.hpp>

// The persistent_term builtins (plan step 41M2, docs/builtins.md#persistent-terms): put/2, put_new/2, get/0,1,2,
// erase/1 and info/0 over the runtime-wide table (docs/runtime.md#persistent-terms). A read returns the stored term
// in place, as OTP's literal persistent terms (persistent terms in place); a missing key of get/1 and a different
// value for put_new/2 are badarg, as in OTP.
namespace clause::runtime::builtins {
namespace {
using detail::PersistentTerms;
using detail::TermAccess;

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
PersistentTerms::SameKey same_key(const Term &key) {
    return [key](Word stored) { return need(TermAccess::make(stored).exactly_equal(key)); };
}

// The stored value of `key`, if any, read in place.
std::optional<Term> lookup(ProcessContext &context, const Term &key) {
    const auto stored = context.persistent_terms().find(term_hash(context, key), same_key(key));
    return stored ? std::optional{TermAccess::make(*stored)} : std::nullopt;
}

// Have the parked processes copy out what a put or erase retired; running and queued ones do at their next slice
// (persistent term reclamation).
void evacuate(ProcessContext &context) {
    if (context.persistent_terms().waiting()) {
        detail::Executor::of(context).evacuate_parked();
    }
}

// Store `value` under `key`.
void store(ProcessContext &context, const Term &key, const Term &value) {
    const auto stored = context.persistent_terms().store(context, term_hash(context, key), key, value, same_key(key));
    if (!stored) {
        throw BuiltinFailure{.term = stored.error()};
    }
    evacuate(context);
}

// put(Key, Value).
TermResult<Term> put(ProcessContext &context, const Term &key, const Term &value) {
    store(context, key, value);
    return TermFactory(context).atom("ok");
}

// put_new(Key, Value): store Value unless Key is stored; ok also when it is stored with an exactly equal value.
TermResult<Term> put_new(ProcessContext &context, const Term &key, const Term &value) {
    if (const auto stored = lookup(context, key)) {
        if (!need(stored->exactly_equal(value))) {
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
    return *stored;
}

// get(Key, Default).
TermResult<Term> get2(ProcessContext &context, const Term &key, const Default &fallback) {
    const auto stored = lookup(context, key);
    return stored ? *stored : fallback.term;
}

// get(): every stored association as {Key, Value}.
TermResult<Term> get0(ProcessContext &context) {
    TermFactory factory(context);
    std::vector<Term> items;
    for (const auto &entry : context.persistent_terms().entries()) {
        const std::array pair{TermAccess::make(entry.key), TermAccess::make(entry.value)};
        items.push_back(need(factory.tuple(pair)));
    }
    return factory.list(items);
}

// erase(Key): whether Key was stored.
TermResult<Term> erase(ProcessContext &context, const Term &key) {
    const auto erased = context.persistent_terms().erase(term_hash(context, key), same_key(key));
    evacuate(context);
    return TermFactory(context).boolean(erased);
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
