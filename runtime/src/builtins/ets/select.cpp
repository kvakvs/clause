#include "select.hpp"
#include <array>
#include <terms.hpp>

// The ets select builtins that read (plan 21 step LIB11, docs/builtins.md#ets): select/1,2,3, select_reverse/1,2,3,
// match/1,2,3, match_object/1,2,3 and select_count/2. A whole-table select reads the table under its locks; a chunked
// one walks a hash table by slots and an ordered_set by keys, and its continuation has OTP's shape, so
// repair_continuation/2 reads it: {Tid, NextSlot, Limit, MatchSpec, Rest, RestCount} for a hash table (NextSlot -1 at
// the end) and {Tid, LastKey, [], Limit, MatchSpec, [], Reverse, 0} for an ordered_set.
namespace clause::runtime::builtins::ets {
namespace {
using detail::ets::Index;
using detail::ets::Step;

// Where a chunked select's walk stands once its results are gathered.
struct Gathered {
    std::vector<Term> results;
    // A hash table's next slot, -1 at the end.
    std::int64_t slot = -1;
    // An ordered_set's last key visited, none at the end.
    std::optional<Term> last_key;
};

// The arguments of a chunked select: its table, program and the handle naming the program in continuations.
struct Chunked {
    std::shared_ptr<Table> table;
    std::shared_ptr<const Program> program;
    Term handle;
    std::size_t limit;
    bool reverse;
};

Term list_of(ProcessContext &context, std::span<const Term> terms) { return need(TermFactory(context).list(terms)); }

Term small_integer(ProcessContext &context, std::int64_t value) { return need(TermFactory(context).integer(value)); }

// Every result of a program on a table, in walk order (reversed for select_reverse on an ordered_set): the bound
// keys' objects at once, else a walk in portions.
Word all_results(ProcessContext &context, const TableArgument &argument, const Term &spec, bool reverse) {
    auto found = table(context, argument, Access::read);
    auto program = compiled(context, spec);
    if (!bound_keys(*found, *program)) {
        return walk_table(context, std::move(found), std::move(program), Gather::results, reverse);
    }
    std::vector<Term> results;
    each_candidate(context, *found, *program, [&](Word object) {
        if (const auto result = run_stored(context, *program, object)) {
            results.push_back(*result);
        }
    });
    if (reverse && !found->hashed()) {
        std::ranges::reverse(results);
    }
    return list_of(context, results).word();
}

// Run the program on the objects of a slot, gathering the results.
void gather(ProcessContext &context, const Chunked &select, std::span<const Word> objects, Gathered &gathered) {
    for (const auto object : objects) {
        if (const auto result = run_stored(context, *select.program, object)) {
            gathered.results.push_back(*result);
        }
    }
}

// Visit slots from `slot` until `limit` results are gathered or the walk ends (OTP stops at slot ends); a walk that
// filled the chunk at the last slot has ended too, as OTP's next_slot wraps there.
void walk_slots(ProcessContext &context, const Chunked &select, Gathered &gathered) {
    auto reached = Reached::objects;
    while (reached == Reached::objects && gathered.results.size() < select.limit) {
        reached = select.table->visit_slot(static_cast<std::size_t>(gathered.slot), [&](std::span<const Word> objects) {
            gather(context, select, objects, gathered);
        });
        need_table(reached != Reached::deleted && reached != Reached::missing);
        ++gathered.slot;
    }
    std::vector<Word> probe;
    if (reached == Reached::end || select.table->slot(static_cast<std::size_t>(gathered.slot), probe) == Reached::end) {
        gathered.slot = -1;
    }
}

// The objects of an ordered_set's key after (or before) `from`, or its first (last) one.
const Objects *step(const Index &index, const std::optional<Term> &from, bool reverse) {
    if (!from) {
        return reverse ? index.last() : index.first();
    }
    const Key at{.word = from->word(), .hash = 0};
    return (reverse ? index.prev(at) : index.next(at)).objects;
}

// Walk an ordered_set's keys from the last key until `limit` results; the last key visited is kept.
void walk_keys(ProcessContext &context, const Chunked &select, Gathered &gathered) {
    const auto keypos = select.table->options().keypos;
    need_table(select.table->read_parts([&](const Index &index) {
        auto last = gathered.last_key;
        for (const auto *objects = step(index, last, select.reverse); objects != nullptr;
             objects = step(index, last, select.reverse)) {
            last = copy_out(context, detail::ets::key_of(objects->front().word(), keypos));
            if (const auto result = run_stored(context, *select.program, objects->front().word())) {
                gathered.results.push_back(*result);
            }
            if (gathered.results.size() == select.limit) {
                gathered.last_key = last;
                return;
            }
        }
        gathered.last_key.reset();
    }));
}

// {Matches, Continuation}, {Matches, '$end_of_table'} or '$end_of_table' for gathered results: a hash table keeps
// the results past the limit for the next call.
Term reply(ProcessContext &context, const Chunked &select, const Gathered &gathered) {
    const auto count = std::min(gathered.results.size(), select.limit);
    const auto matches = list_of(context, std::span(gathered.results).first(count));
    const auto rest = std::span(gathered.results).subspan(count);
    const auto tid = identifier(context, *select.table);
    std::optional<Term> continuation;
    if (select.table->hashed() && (!rest.empty() || gathered.slot >= 0)) {
        const std::array fields{tid,
                                small_integer(context, gathered.slot),
                                small_integer(context, static_cast<std::int64_t>(select.limit)),
                                select.handle,
                                list_of(context, rest),
                                small_integer(context, static_cast<std::int64_t>(rest.size()))};
        continuation = need(TermFactory(context).tuple(fields));
    } else if (!select.table->hashed() && gathered.last_key) {
        const auto nil = need(TermFactory(context).nil());
        const std::array fields{tid,
                                *gathered.last_key,
                                nil,
                                small_integer(context, static_cast<std::int64_t>(select.limit)),
                                select.handle,
                                nil,
                                small_integer(context, select.reverse ? 1 : 0),
                                small_integer(context, 0)};
        continuation = need(TermFactory(context).tuple(fields));
    }
    if (!continuation && count == 0) {
        return end_of_table(context);
    }
    return need(TermFactory(context).tuple(std::array{matches, continuation.value_or(end_of_table(context))}));
}

// The chunk of a select whose heads bind their keys: every result at once, an ordered_set's in one chunk (OTP looks
// a single key up), a hash table's split by the limit.
Term bound_chunk(ProcessContext &context, const Chunked &select) {
    Gathered gathered{.results = {}, .slot = -1, .last_key = std::nullopt};
    each_candidate(context, *select.table, *select.program,
                   [&](Word object) { gather(context, select, std::span(&object, 1), gathered); });
    if (!select.table->hashed() && !gathered.results.empty()) {
        const auto matches = list_of(context, gathered.results);
        return need(TermFactory(context).tuple(std::array{matches, end_of_table(context)}));
    }
    return reply(context, select, gathered);
}

// A chunk of a select: the bound keys' objects at once (an ordered_set's single key), else a walk.
Term chunk(ProcessContext &context, const Chunked &select, Gathered gathered) {
    const auto keys = bound_keys(*select.table, *select.program);
    if (keys && (select.table->hashed() || keys->size() <= 1)) {
        return bound_chunk(context, select);
    }
    if (select.table->hashed()) {
        walk_slots(context, select, gathered);
    } else {
        walk_keys(context, select, gathered);
    }
    return reply(context, select, gathered);
}

// A positive small integer limit; badarg otherwise.
std::size_t limit_of(const Term &term) {
    const auto value = small(term.word());
    if (!value || *value <= 0) {
        bad_argument();
    }
    return static_cast<std::size_t>(*value);
}

Term first_chunk(ProcessContext &context, const TableArgument &argument, const SpecArgument &spec,
                 const LimitArgument &limit, bool reverse) {
    auto found = table(context, argument, Access::read);
    auto program = compiled(context, spec.term);
    const auto size = limit_of(limit.term);
    const auto handle = need(detail::match_spec::handle(context, program));
    const Chunked select{
        .table = std::move(found), .program = std::move(program), .handle = handle, .limit = size, .reverse = reverse};
    return chunk(context, select, {.results = {}, .slot = 0, .last_key = std::nullopt});
}

// A non-negative small integer element of a continuation; badarg otherwise.
std::int64_t counted(const Term &term, std::int64_t least) {
    const auto value = small(term.word());
    if (!value || *value < least) {
        bad_argument();
    }
    return *value;
}

// The results a hash continuation kept, checked against the count it gives.
std::vector<Term> kept_results(const Term &rest, std::int64_t count) {
    std::vector<Term> results;
    auto cell = rest;
    for (; cell.is_cons(); cell = cell.tail_unchecked()) {
        results.push_back(cell.head_unchecked());
    }
    if (!cell.is_nil() || static_cast<std::int64_t>(results.size()) != count) {
        bad_argument();
    }
    return results;
}

// The select a continuation names: its table (of the continuation's kind), program, limit and direction; badarg for
// anything else.
Chunked continued(ProcessContext &context, const Term &continuation) {
    const auto size = continuation.is_tuple() ? continuation.tuple_size_unchecked() : 0;
    if (size != 6 && size != 8) {
        bad_argument();
    }
    const auto hashed = size == 6;
    const auto element = [&](std::size_t at) { return continuation.tuple_element_unchecked(at); };
    auto found = table(context, {element(0)}, Access::read);
    const auto handle = element(hashed ? 3 : 4);
    auto program = detail::match_spec::program_of(handle);
    if (hashed != found->hashed() || !program) {
        bad_argument();
    }
    return {.table = std::move(found),
            .program = std::move(program),
            .handle = handle,
            .limit = static_cast<std::size_t>(counted(element(hashed ? 2 : 3), 1)),
            .reverse = !hashed && counted(element(6), 0) != 0};
}

// The next chunk of a hash continuation: the results it kept first, then the walk from its slot (none at -1).
Term next_hash_chunk(ProcessContext &context, const Chunked &select, const Term &continuation) {
    const auto element = [&](std::size_t at) { return continuation.tuple_element_unchecked(at); };
    Gathered gathered{.results = kept_results(element(4), counted(element(5), 0)),
                      .slot = counted(element(1), -1),
                      .last_key = std::nullopt};
    if (gathered.slot >= 0) {
        walk_slots(context, select, gathered);
    }
    return reply(context, select, gathered);
}

// The select a continuation goes on with.
Term next_chunk(ProcessContext &context, const Term &continuation) {
    const auto select = continued(context, continuation);
    if (select.table->hashed()) {
        return next_hash_chunk(context, select, continuation);
    }
    Gathered gathered{.results = {}, .slot = -1, .last_key = continuation.tuple_element_unchecked(1)};
    walk_keys(context, select, gathered);
    return reply(context, select, gathered);
}

Word select2(ProcessContext &context, const TableArgument &argument, const Term &spec) {
    return all_results(context, argument, spec, false);
}

Word select_reverse2(ProcessContext &context, const TableArgument &argument, const Term &spec) {
    return all_results(context, argument, spec, true);
}

Word match2(ProcessContext &context, const TableArgument &argument, const Term &pattern) {
    return all_results(context, argument, pattern_spec(context, pattern, "$$"_atom), false);
}

Word match_object2(ProcessContext &context, const TableArgument &argument, const Term &pattern) {
    return all_results(context, argument, pattern_spec(context, pattern, "$_"_atom), false);
}

Term select3(ProcessContext &context, const TableArgument &argument, const SpecArgument &spec,
             const LimitArgument &limit) {
    return first_chunk(context, argument, spec, limit, false);
}

Term select_reverse3(ProcessContext &context, const TableArgument &argument, const SpecArgument &spec,
                     const LimitArgument &limit) {
    return first_chunk(context, argument, spec, limit, true);
}

Term match3(ProcessContext &context, const TableArgument &argument, const SpecArgument &pattern,
            const LimitArgument &limit) {
    return first_chunk(context, argument, {pattern_spec(context, pattern.term, "$$"_atom)}, limit, false);
}

Term match_object3(ProcessContext &context, const TableArgument &argument, const SpecArgument &pattern,
                   const LimitArgument &limit) {
    return first_chunk(context, argument, {pattern_spec(context, pattern.term, "$_"_atom)}, limit, false);
}

// select/1, select_reverse/1, match/1 and match_object/1 of a continuation.
Term select1(ProcessContext &context, const Term &continuation) {
    if (spelling(continuation) == "$end_of_table") {
        return continuation;
    }
    return next_chunk(context, continuation);
}

// select_count(Table, MatchSpec): the objects for which the specification gives true.
Word select_count(ProcessContext &context, const TableArgument &argument, const Term &spec) {
    auto found = table(context, argument, Access::read);
    auto program = compiled(context, spec);
    if (!bound_keys(*found, *program)) {
        return walk_table(context, std::move(found), std::move(program), Gather::count, false);
    }
    std::int64_t count = 0;
    each_candidate(context, *found, *program, [&](Word object) {
        const auto result = run_stored(context, *program, object);
        count += result && result->word() == abi::v1::true_atom ? 1 : 0;
    });
    return small_integer(context, count).word();
}

constexpr std::array ETS_SELECT_BUILTINS{
    typed_entry<select2>("ets", "select"),
    typed_entry<select3>("ets", "select"),
    typed_entry<select1>("ets", "select"),
    typed_entry<select_reverse2>("ets", "select_reverse"),
    typed_entry<select_reverse3>("ets", "select_reverse"),
    typed_entry<select1>("ets", "select_reverse"),
    typed_entry<match2>("ets", "match"),
    typed_entry<match3>("ets", "match"),
    typed_entry<select1>("ets", "match"),
    typed_entry<match_object2>("ets", "match_object"),
    typed_entry<match_object3>("ets", "match_object"),
    typed_entry<select1>("ets", "match_object"),
    typed_entry<select_count>("ets", "select_count"),
};
} // namespace
} // namespace clause::runtime::builtins::ets

namespace clause::runtime {
std::span<const BuiltinEntry> ets_select_builtins() noexcept { return builtins::ets::ETS_SELECT_BUILTINS; }
} // namespace clause::runtime
