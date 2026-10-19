#include "../portions.hpp"
#include "select.hpp"
#include <chrono>
#include <terms.hpp>

// Whole-table reads in portions (plan 21 step LIB10F, docs/runtime.md#ets-tables): tab2list/1 and the selects of
// every object walk a hash table slot by slot and an ordered_set key by key, as many objects a portion as the
// process's budget allows, then trap. A hash table is fixed by the walking process from its first trap to its end, as
// OTP fixes it, so its slots stay put and each key is visited once.
namespace clause::runtime::builtins::ets {
namespace {
using detail::ets::Index;

// Budget units one object takes: its visit, and copying what it gives.
constexpr std::size_t OBJECT_WORK = 4;

// A walk between its portions; words() holds the results so far, the last one first, and an ordered_set walk's last
// key.
struct Walk final : TrapState {
    std::shared_ptr<Table> table;
    std::shared_ptr<const Program> program;
    Gather gather = Gather::objects;
    bool reverse = false;
    std::int64_t slot = 0;
    bool fixed = false;
    bool started = false;
    std::int64_t count = 0;
};

Word walk_continue(ProcessContext &context, Arguments);

constexpr BuiltinFrame WALK_FRAME = continuation_frame(&guarded<walk_continue>, 0);

// What one stored object gives the walk.
void gather_object(ProcessContext &context, Walk &walk, Word object) {
    if (walk.gather == Gather::objects) {
        const auto copy = copy_out(context, object);
        walk.words()[0] = need(TermFactory(context).cons(copy, detail::TermAccess::make(walk.words()[0]))).word();
        return;
    }
    const auto result = run_stored(context, *walk.program, object);
    if (!result) {
        return;
    }
    if (walk.gather == Gather::count) {
        walk.count += result->word() == abi::v1::true_atom ? 1 : 0;
        return;
    }
    walk.words()[0] = need(TermFactory(context).cons(*result, detail::TermAccess::make(walk.words()[0]))).word();
}

// One portion over a hash table's slots; whether the walk reached the end.
bool hash_portion(ProcessContext &context, Walk &walk, std::size_t &budget) {
    while (budget > 0) {
        const auto reached =
            walk.table->visit_slot(static_cast<std::size_t>(walk.slot), [&](std::span<const Word> objects) {
                for (const auto object : objects) {
                    gather_object(context, walk, object);
                }
                budget -= std::min(budget, 1 + (objects.size() * OBJECT_WORK));
            });
        need_table(reached != Reached::deleted && reached != Reached::missing);
        if (reached == Reached::end) {
            return true;
        }
        ++walk.slot;
    }
    return false;
}

// The objects of an ordered_set's key after (or before) the last one, or its first (last) one.
const Objects *next_objects(const Index &index, const Walk &walk, Word last) {
    if (!walk.started) {
        return walk.reverse ? index.last() : index.first();
    }
    const Key at{.word = last, .hash = 0};
    return (walk.reverse ? index.prev(at) : index.next(at)).objects;
}

// One portion over an ordered_set's keys; the last key is copied out when the portion stops before the end.
bool tree_portion(ProcessContext &context, Walk &walk, std::size_t &budget) {
    const auto keypos = walk.table->options().keypos;
    auto ended = false;
    need_table(walk.table->read_parts([&](const Index &index) {
        auto last = walk.words()[1];
        for (; budget > 0; budget -= std::min(budget, OBJECT_WORK)) {
            const auto *objects = next_objects(index, walk, last);
            if (objects == nullptr) {
                ended = true;
                return;
            }
            gather_object(context, walk, objects->front().word());
            last = detail::ets::key_of(objects->front().word(), keypos);
            walk.started = true;
        }
        walk.words()[1] = walk.started ? copy_out(context, last).word() : last;
    }));
    return ended;
}

// The walk's value once it ended: the list in walk order, or the count; its fixation ends.
Word finish(ProcessContext &context, Walk &walk) {
    if (walk.fixed) {
        static_cast<void>(context.ets_tables().unfix(*walk.table, self(context)));
    }
    TermFactory factory(context);
    if (walk.gather == Gather::count) {
        return need(factory.integer(walk.count)).word();
    }
    return need(factory.reverse(detail::TermAccess::make(walk.words()[0]), need(factory.nil()))).word();
}

// Run one portion; the value at the end, else 0 after trapping (fixing a hash table at its first trap).
Word portion(ProcessContext &context, std::unique_ptr<Walk> owned, Walk &walk) {
    auto &stack = context.stack();
    const auto budget = stack.budget();
    auto left = budget;
    const auto ended = walk.table->hashed() ? hash_portion(context, walk, left) : tree_portion(context, walk, left);
    stack.spend(budget - left);
    if (ended) {
        const auto value = finish(context, walk);
        stack.drop_trap_state();
        return value;
    }
    if (walk.table->hashed() && !walk.fixed) {
        using namespace std::chrono;
        const detail::ets::FixTime time{
            .monotonic_nanos = duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(),
            .system_micros = duration_cast<microseconds>(system_clock::now().time_since_epoch()).count()};
        need_table(context.ets_tables().fix(*walk.table, self(context), time));
        walk.fixed = true;
    }
    if (owned) {
        stack.keep_trap_state(std::move(owned));
    }
    stack.trap(WALK_FRAME.frame, {});
    return 0;
}

Word walk_continue(ProcessContext &context, Arguments) {
    auto &walk = trap_state<Walk>(context);
    return portion(context, nullptr, walk);
}
} // namespace

Word walk_table(ProcessContext &context, std::shared_ptr<Table> table, std::shared_ptr<const Program> program,
                Gather gather, bool reverse) {
    auto walk = std::make_unique<Walk>();
    walk->table = std::move(table);
    walk->program = std::move(program);
    walk->gather = gather;
    walk->reverse = reverse && !walk->table->hashed();
    walk->words() = {need(TermFactory(context).nil()).word(), need(TermFactory(context).nil()).word()};
    auto &state = *walk;
    return portion(context, std::move(walk), state);
}
} // namespace clause::runtime::builtins::ets
