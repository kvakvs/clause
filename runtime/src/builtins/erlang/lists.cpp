#include "../../terms/structural_order.hpp"
#include "../portions.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <boost/container/small_vector.hpp>
#include <clause/abi/equality.hpp>
#include <optional>
#include <span>

// The list builtins that run in bounded portions (docs/builtins.md#portions): length/1, '++'/2 and '--'/2. Each
// portion does at most the process stack's budget of work and traps to a continuation, keeping its state rooted.
namespace clause::runtime::builtins {
namespace {
Word length_continue(ProcessContext &context, Arguments state);
Word append_continue(ProcessContext &context, Arguments state);
Word subtract_collect(ProcessContext &context, Arguments state);
Word subtract_sort(ProcessContext &context, Arguments state);
Word subtract_scan(ProcessContext &context, Arguments state);

// Continuations: length [Rest, Counted]; ++ [Rest, Right]; -- collecting [Left, RestRight], sorting [Left] and
// scanning [Left, RestLeft].
constexpr BuiltinFrame LENGTH_FRAME = continuation_frame(&guarded<length_continue>, 2);
constexpr BuiltinFrame APPEND_FRAME = continuation_frame(&guarded<append_continue>, 2);
constexpr BuiltinFrame SUBTRACT_COLLECT_FRAME = continuation_frame(&guarded<subtract_collect>, 2);
constexpr BuiltinFrame SUBTRACT_SORT_FRAME = continuation_frame(&guarded<subtract_sort>, 1);
constexpr BuiltinFrame SUBTRACT_SCAN_FRAME = continuation_frame(&guarded<subtract_scan>, 2);

// Count the cells of `rest` in portions, `counted` already counted; an improper list is badarg.
Word count(ProcessContext &context, Term rest, std::size_t counted) {
    auto &stack = context.stack();
    const auto walked = walk(rest, stack.budget(), [](Word) {});
    stack.spend(walked);
    counted += walked;
    if (rest.is_cons()) {
        stack.trap(LENGTH_FRAME.frame, std::array{rest.word(), count_word(context, counted)});
        return 0;
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return count_word(context, counted);
}

// length(List).
Word length(ProcessContext &context, Arguments arguments) {
    const auto list = term_of(context, arguments[0]);
    if (!list.is_list()) {
        bad_argument();
    }
    return count(context, list, 0);
}

Word length_continue(ProcessContext &context, Arguments state) {
    return count(context, term_of(context, state[0]), count_of(context, state[1]));
}

// Collect the elements of Left from `rest` in portions, then build the copy onto Right; an improper Left is badarg.
Word append_portion(ProcessContext &context, Term rest, const Term &right) {
    auto &stack = context.stack();
    auto &words = trap_state<ListState>(context).words();
    const auto walked = walk(rest, stack.budget(), [&](Word head) { words.push_back(head); });
    stack.spend(walked);
    if (rest.is_cons()) {
        stack.trap(APPEND_FRAME.frame, std::array{rest.word(), right.word()});
        return 0;
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return build_list(context, right);
}

// Left lists up to this many cells are copied at once, without portion state (short appends).
constexpr std::size_t SHORT_APPEND = 64;

// A short Left copied onto Right in one step; none when Left is longer and needs portions.
std::optional<Word> short_append(ProcessContext &context, Term rest, const Word right) {
    boost::container::small_vector<Word, SHORT_APPEND> words;
    const auto walked = walk(rest, SHORT_APPEND, [&](Word head) { words.push_back(head); });
    if (rest.is_cons()) {
        return std::nullopt;
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    context.stack().spend(2 * walked);
    return need(TermFactory(context).list_words(words, term_of(context, right))).word();
}

// Left ++ Right: Left must be a proper list; [] ++ Right is Right whatever it is.
Word append(ProcessContext &context, Arguments arguments) {
    const auto left = term_of(context, arguments[0]);
    const auto right = term_of(context, arguments[1]);
    if (left.is_nil()) {
        return right.word();
    }
    if (!left.is_cons()) {
        bad_argument();
    }
    if (const auto copied = short_append(context, left, right.word())) {
        return *copied;
    }
    context.stack().keep_trap_state(std::make_unique<ListState>());
    return append_portion(context, left, right);
}

Word append_continue(ProcessContext &context, Arguments state) {
    return append_portion(context, term_of(context, state[0]), term_of(context, state[1]));
}

// The state of Left -- Right: Right's elements sorted by exact order in the first `size` words (a scratch copy follows
// while sorting, then Left's kept elements), how many of each run of equal elements are removed, and the sort.
class SubtractState final : public ListState {
  public:
    // Exact term order of two state words: true when `left` sorts first.
    bool before(ProcessContext &context, Word left, Word right) const;

    // Number of Right's elements; the kept elements of Left follow them.
    std::size_t size = 0;
    // Bottom-up merge sort position: run width, start of the run pair being merged, read and write cursors, and
    // whether the sorted runs are in the scratch copy.
    std::size_t width = 1;
    std::size_t start = 0;
    std::size_t left = 0;
    std::size_t right = 0;
    std::size_t out = 0;
    bool merging = false;
    bool in_scratch = false;
    // Removed occurrences per run of equal elements, indexed by the run's first position.
    std::vector<std::size_t> removed;
    // Whether any element of Left was removed; when none was, the result is Left itself.
    bool changed = false;
};

bool SubtractState::before(ProcessContext &context, Word left, Word right) const {
    return need(detail::structural_order(term_of(context, left), term_of(context, right), true)) < 0;
}

// Begin merging the run pair at `state.start` of the current width.
void open_runs(SubtractState &state) {
    state.left = state.start;
    state.right = std::min(state.start + state.width, state.size);
    state.out = state.start;
    state.merging = true;
}

// Copy what is left of the run pair once one run is used up, and move on to the next pair.
void close_runs(SubtractState &state, std::size_t middle, std::size_t end) {
    auto &words = state.words();
    const auto from = state.in_scratch ? state.size : 0;
    const auto to = state.size - from;
    const auto left = std::span(words).subspan(from + state.left, middle - state.left);
    const auto right = std::span(words).subspan(from + state.right, end - state.right);
    const auto rest = std::ranges::copy(left, words.begin() + static_cast<std::ptrdiff_t>(to + state.out)).out;
    std::ranges::copy(right, rest);
    state.merging = false;
    state.start = end;
}

// Merge the open run pair for at most `budget` comparisons; returns the comparisons made.
std::size_t merge(ProcessContext &context, SubtractState &state, std::size_t budget) {
    auto &words = state.words();
    const auto from = state.in_scratch ? state.size : 0;
    const auto to = state.size - from;
    const auto middle = std::min(state.start + state.width, state.size);
    const auto end = std::min(state.start + 2 * state.width, state.size);
    std::size_t compared = 0;
    for (; compared < budget && state.left < middle && state.right < end; ++compared) {
        const bool take_right = state.before(context, words[from + state.right], words[from + state.left]);
        words[to + state.out++] = words[from + (take_right ? state.right++ : state.left++)];
    }
    if (state.left == middle || state.right == end) {
        close_runs(state, middle, end);
    }
    return compared;
}

// Advance the sort by at most `budget` comparisons; true once Right's elements are sorted in the first words.
bool sort_step(ProcessContext &context, SubtractState &state, std::size_t &budget) {
    while (budget > 0 && state.width < state.size) {
        if (state.start >= state.size) {
            state.width *= 2;
            state.start = 0;
            state.in_scratch = !state.in_scratch;
        } else if (!state.merging) {
            open_runs(state);
        } else {
            budget -= merge(context, state, budget);
        }
    }
    return state.width >= state.size;
}

// Whether `item` removes an occurrence of Right still left; consumes it when it does.
bool consume(ProcessContext &context, SubtractState &state, Word item) {
    const auto sorted = std::span(state.words()).first(state.size);
    const auto found = std::ranges::lower_bound(
        sorted, item, [&](Word left, Word right) { return state.before(context, left, right); });
    const auto run = static_cast<std::size_t>(found - sorted.begin());
    // Equal elements form one run from `run`; the next one left is equal unless it sorts after `item`.
    const auto at = run + (run < state.size ? state.removed[run] : 0);
    if (at >= state.size || state.before(context, item, sorted[at])) {
        return false;
    }
    ++state.removed[run];
    return true;
}

// Scan Left from `rest` in portions, keeping what Right does not remove, then build the result.
Word scan(ProcessContext &context, const Term &left, Term rest) {
    auto &stack = context.stack();
    auto &state = trap_state<SubtractState>(context);
    // Each element costs the comparisons of a binary search among Right's elements.
    const auto cost = state.size == 0 ? 1 : 1 + static_cast<std::size_t>(std::bit_width(state.size));
    const auto walked = walk(rest, std::max<std::size_t>(stack.budget() / cost, 1), [&](Word head) {
        if (state.size != 0 && consume(context, state, head)) {
            state.changed = true;
        } else {
            state.words().push_back(head);
        }
    });
    stack.spend(walked * cost);
    if (rest.is_cons()) {
        stack.trap(SUBTRACT_SCAN_FRAME.frame, std::array{left.word(), rest.word()});
        return 0;
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    if (!state.changed) {
        stack.drop_trap_state();
        return left.word();
    }
    state.base = state.size;
    return build_list(context, need(Term::from_word(abi::v1::empty_list)));
}

// Sort Right's elements in portions, then scan Left.
Word sort(ProcessContext &context, const Term &left) {
    auto &stack = context.stack();
    auto &state = trap_state<SubtractState>(context);
    auto budget = stack.budget();
    const auto before = budget;
    const bool sorted = sort_step(context, state, budget);
    stack.spend(before - budget);
    if (!sorted) {
        stack.trap(SUBTRACT_SORT_FRAME.frame, std::array{left.word()});
        return 0;
    }
    auto &words = state.words();
    if (state.in_scratch) {
        std::copy_n(words.begin() + static_cast<std::ptrdiff_t>(state.size), state.size, words.begin());
    }
    words.resize(state.size);
    state.removed.assign(state.size, 0);
    return scan(context, left, left);
}

// Collect Right's elements from `rest` in portions with a scratch word each, then sort them.
Word collect(ProcessContext &context, const Term &left, Term rest) {
    auto &stack = context.stack();
    auto &state = trap_state<SubtractState>(context);
    auto &words = state.words();
    const auto walked = walk(rest, stack.budget(), [&](Word head) { words.push_back(head); });
    stack.spend(walked);
    if (rest.is_cons()) {
        stack.trap(SUBTRACT_COLLECT_FRAME.frame, std::array{left.word(), rest.word()});
        return 0;
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    state.size = words.size();
    words.resize(2 * state.size, abi::v1::empty_list);
    return sort(context, left);
}

// Left -- Right: both proper lists; each element of Right removes the first exactly equal element of Left.
Word subtract(ProcessContext &context, Arguments arguments) {
    const auto left = term_of(context, arguments[0]);
    const auto right = term_of(context, arguments[1]);
    if (!left.is_list() || !right.is_list()) {
        bad_argument();
    }
    context.stack().keep_trap_state(std::make_unique<SubtractState>());
    return collect(context, left, right);
}

Word subtract_collect(ProcessContext &context, Arguments state) {
    return collect(context, term_of(context, state[0]), term_of(context, state[1]));
}

Word subtract_sort(ProcessContext &context, Arguments state) { return sort(context, term_of(context, state[0])); }

Word subtract_scan(ProcessContext &context, Arguments state) {
    return scan(context, term_of(context, state[0]), term_of(context, state[1]));
}

constexpr std::array LIST_BUILTINS{
    BuiltinEntry{"erlang", "length", 1, &guarded<length>},
    BuiltinEntry{"erlang", "++", 2, &guarded<append>},
    BuiltinEntry{"erlang", "--", 2, &guarded<subtract>},
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> list_builtins() noexcept { return builtins::LIST_BUILTINS; }
} // namespace clause::runtime
