#include "collections.hpp"
#include <algorithm>
#include <array>
#include <ranges>

// The queue builtins (plan 21 step LIB3, docs/builtins.md#collections): OTP's {Rear, Front} pair of lists, the rear
// newest first. Each operation moves elements between the lists exactly as OTP's does, so queues equal OTP's; a
// term that is not a pair of lists raises badarg, an empty queue's get/drop the error empty.
namespace clause::runtime::builtins::collections {
namespace {
struct QueueRole;
struct CountRole;
// A queue argument, and split/2's count.
using Queue = Named<QueueRole>;
using Count = Named<CountRole>;

// A queue's lists.
struct Lists {
    std::vector<Term> rear;
    std::vector<Term> front;
};

Term atom(ProcessContext &context, std::string_view name) { return need(TermFactory(context).atom(name)); }

Term tuple(ProcessContext &context, std::span<const Term> parts) { return need(TermFactory(context).tuple(parts)); }

Term list(ProcessContext &context, std::span<const Term> elements) { return need(TermFactory(context).list(elements)); }

// The elements of a list, or none for anything else (OTP's is_list checks the first cell only; a later improper
// tail raises badarg where OTP's length/1 or ++ would).
std::optional<std::vector<Term>> items(const Term &term) {
    if (!term.is_list()) {
        return std::nullopt;
    }
    std::vector<Term> elements;
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        elements.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return elements;
}

// A queue's lists; badarg for anything else.
Lists lists_of(const Queue &queue) {
    if (!queue.term.is_tuple() || queue.term.tuple_size_unchecked() != 2) {
        bad_argument();
    }
    auto rear = items(queue.term.tuple_element_unchecked(0));
    auto front = items(queue.term.tuple_element_unchecked(1));
    if (!rear || !front) {
        bad_argument();
    }
    return {std::move(*rear), std::move(*front)};
}

Term queue_of(ProcessContext &context, std::span<const Term> rear, std::span<const Term> front) {
    return tuple(context, std::array{list(context, rear), list(context, front)});
}

// OTP's f2r: a front list as a queue, its second half reversed into the rear (one element stays in the rear, two
// are split).
Term from_front(ProcessContext &context, std::vector<Term> front) {
    if (front.size() <= 1) {
        return queue_of(context, front, {});
    }
    const auto half = front.size() / 2;
    std::vector<Term> rear(front.begin() + static_cast<std::ptrdiff_t>(half), front.end());
    std::ranges::reverse(rear);
    front.resize(half);
    return queue_of(context, rear, front);
}

// OTP's r2f: a rear list as a queue, its second half reversed into the front.
Term from_rear(ProcessContext &context, std::vector<Term> rear) {
    if (rear.size() <= 1) {
        return queue_of(context, {}, rear);
    }
    const auto half = rear.size() / 2;
    std::vector<Term> front(rear.begin() + static_cast<std::ptrdiff_t>(half), rear.end());
    std::ranges::reverse(front);
    rear.resize(half);
    return queue_of(context, rear, front);
}

// error(empty), raised by get/1, get_r/1, drop/1, drop_r/1 and head/1 of an empty queue.
[[noreturn]] void raise_empty(ProcessContext &context) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = atom(context, "empty").word()};
}

// to_list(Q): front first, then the rear reversed.
Term to_list(ProcessContext &context, const Queue &queue) {
    auto [rear, front] = lists_of(queue);
    front.insert(front.end(), rear.rbegin(), rear.rend());
    return list(context, front);
}

// from_list(L): badarg unless a list.
Term from_list(ProcessContext &context, const List &elements) {
    auto front = items(elements.term);
    if (!front) {
        bad_argument();
    }
    return from_front(context, std::move(*front));
}

// member(Item, Q): exact matching (=:=).
Term member(ProcessContext &context, const Element &item, const Queue &queue) {
    const auto [rear, front] = lists_of(queue);
    const auto has = [&](const std::vector<Term> &side) {
        return std::ranges::any_of(side, [&](const Term &element) { return same(element, item.term); });
    };
    return need(TermFactory(context).boolean(has(rear) || has(front)));
}

// in(Item, Q1): at the rear; a one-element rear with an empty front moves to the front.
Term in(ProcessContext &context, const Element &item, const Queue &queue) {
    auto [rear, front] = lists_of(queue);
    if (rear.size() == 1 && front.empty()) {
        return queue_of(context, std::array{item.term}, rear);
    }
    rear.insert(rear.begin(), item.term);
    return queue_of(context, rear, front);
}

// in_r(Item, Q1): at the front.
Term in_r(ProcessContext &context, const Element &item, const Queue &queue) {
    auto [rear, front] = lists_of(queue);
    if (rear.empty() && front.size() == 1) {
        return queue_of(context, front, std::array{item.term});
    }
    front.insert(front.begin(), item.term);
    return queue_of(context, rear, front);
}

// The front element taken out: {Value, Q2} parts, OTP's out/1 rebalancing; none when empty.
std::optional<std::pair<Term, Term>> take_front(ProcessContext &context, Lists lists) {
    auto &[rear, front] = lists;
    if (rear.empty() && front.empty()) {
        return std::nullopt;
    }
    if (front.empty()) {
        if (rear.size() == 1) {
            return std::pair{rear.front(), queue_of(context, {}, {})};
        }
        std::vector<Term> rest(rear.rbegin(), rear.rend() - 1);
        return std::pair{rest.front(), queue_of(context, std::array{rear.front()}, std::span(rest).subspan(1))};
    }
    const auto value = front.front();
    if (front.size() == 1) {
        return std::pair{value, from_rear(context, rear)};
    }
    return std::pair{value, queue_of(context, rear, std::span(front).subspan(1))};
}

// The rear element taken out (out_r/1).
std::optional<std::pair<Term, Term>> take_rear(ProcessContext &context, Lists lists) {
    auto &[rear, front] = lists;
    if (rear.empty() && front.empty()) {
        return std::nullopt;
    }
    if (rear.empty()) {
        if (front.size() == 1) {
            return std::pair{front.front(), queue_of(context, {}, {})};
        }
        std::vector<Term> rest(front.rbegin(), front.rend() - 1);
        return std::pair{rest.front(), queue_of(context, std::span(rest).subspan(1), std::array{front.front()})};
    }
    const auto value = rear.front();
    if (rear.size() == 1) {
        return std::pair{value, from_front(context, front)};
    }
    return std::pair{value, queue_of(context, std::span(rear).subspan(1), front)};
}

// {{value, Item}, Q2} or {empty, Q1}.
Term taken(ProcessContext &context, const Queue &queue, const std::optional<std::pair<Term, Term>> &result) {
    if (!result) {
        return tuple(context, std::array{atom(context, "empty"), queue.term});
    }
    return tuple(context,
                 std::array{tuple(context, std::array{atom(context, "value"), result->first}), result->second});
}

Term out(ProcessContext &context, const Queue &queue) {
    return taken(context, queue, take_front(context, lists_of(queue)));
}

Term out_r(ProcessContext &context, const Queue &queue) {
    return taken(context, queue, take_rear(context, lists_of(queue)));
}

// The front item: the front's head, else the rear's last (a one-element rear in a well-formed queue).
std::optional<Term> first_item(const Lists &lists) {
    if (!lists.front.empty()) {
        return lists.front.front();
    }
    return lists.rear.empty() ? std::nullopt : std::optional{lists.rear.back()};
}

std::optional<Term> last_item(const Lists &lists) {
    if (!lists.rear.empty()) {
        return lists.rear.front();
    }
    return lists.front.empty() ? std::nullopt : std::optional{lists.front.back()};
}

// get(Q), head(Q): error(empty) when empty.
Term get(ProcessContext &context, const Queue &queue) {
    const auto item = first_item(lists_of(queue));
    if (!item) {
        raise_empty(context);
    }
    return *item;
}

Term get_r(ProcessContext &context, const Queue &queue) {
    const auto item = last_item(lists_of(queue));
    if (!item) {
        raise_empty(context);
    }
    return *item;
}

// peek(Q), peek_r(Q): {value, Item} or empty.
Term peeked(ProcessContext &context, const std::optional<Term> &item) {
    return item ? tuple(context, std::array{atom(context, "value"), *item}) : atom(context, "empty");
}

Term peek(ProcessContext &context, const Queue &queue) { return peeked(context, first_item(lists_of(queue))); }

Term peek_r(ProcessContext &context, const Queue &queue) { return peeked(context, last_item(lists_of(queue))); }

// drop(Q1), drop_r(Q1), tail/1, init/1, liat/1, lait/1: error(empty) when empty.
Term drop(ProcessContext &context, const Queue &queue) {
    const auto result = take_front(context, lists_of(queue));
    if (!result) {
        raise_empty(context);
    }
    return result->second;
}

Term drop_r(ProcessContext &context, const Queue &queue) {
    const auto result = take_rear(context, lists_of(queue));
    if (!result) {
        raise_empty(context);
    }
    return result->second;
}

// join(Q1, Q2): Q1's items, then Q2's; an empty side gives the other queue as it is.
Term join(ProcessContext &context, const Queue &first, const List &second) {
    auto [rear1, front1] = lists_of(first);
    auto [rear2, front2] = lists_of({second.term});
    if (rear2.empty() && front2.empty()) {
        return first.term;
    }
    if (rear1.empty() && front1.empty()) {
        return second.term;
    }
    front1.insert(front1.end(), rear1.rbegin(), rear1.rend());
    front1.insert(front1.end(), front2.begin(), front2.end());
    return queue_of(context, rear2, front1);
}

// split(N, Q1): {Q2, Q3}, the first N items in Q2, built as OTP's moves of single items between the lists;
// badarg past the length.
Term split(ProcessContext &context, const Count &count, const Queue &queue) {
    const auto n = small(count.term.word());
    if (!n || *n < 0) {
        bad_argument();
    }
    auto [rear, front] = lists_of(queue);
    const auto at = static_cast<std::size_t>(*n);
    if (at == 0) {
        return tuple(context, std::array{queue_of(context, {}, {}), queue.term});
    }
    if (at < front.size()) {
        std::vector<Term> taken(front.begin(), front.begin() + static_cast<std::ptrdiff_t>(at));
        std::vector<Term> taken_rear(taken.begin() + 1, taken.end());
        std::ranges::reverse(taken_rear);
        return tuple(context, std::array{queue_of(context, taken_rear, std::array{taken.front()}),
                                         queue_of(context, rear, std::span(front).subspan(at))});
    }
    if (at == front.size()) {
        return tuple(context, std::array{from_front(context, front), from_rear(context, rear)});
    }
    const auto from_rear_count = at - front.size();
    if (from_rear_count > rear.size()) {
        bad_argument();
    }
    const auto keep = rear.size() - from_rear_count;
    if (keep == 0) {
        return tuple(context, std::array{queue.term, queue_of(context, {}, {})});
    }
    std::vector<Term> moved(rear.begin() + 1, rear.begin() + static_cast<std::ptrdiff_t>(keep));
    std::ranges::reverse(moved);
    return tuple(context, std::array{queue_of(context, std::span(rear).subspan(keep), front),
                                     queue_of(context, std::array{rear.front()}, moved)});
}

// The list without its first item exactly equal (=:=) to `item` from the front (delete) or the back (rear); none
// when absent.
std::optional<std::vector<Term>> without(std::vector<Term> side, const Term &item, bool from_back) {
    const auto matches = [&](const Term &element) { return same(element, item); };
    if (from_back) {
        const auto found = std::ranges::find_if(side | std::views::reverse, matches);
        if (found == (side | std::views::reverse).end()) {
            return std::nullopt;
        }
        side.erase(std::next(found).base());
        return side;
    }
    const auto found = std::ranges::find_if(side, matches);
    if (found == side.end()) {
        return std::nullopt;
    }
    side.erase(found);
    return side;
}

// A queue whose lists may be swapped (delete_r/2 works on the reversed queue): rebuilt in the caller's view.
struct View {
    ProcessContext &context;
    bool reversed;

    Term build(std::span<const Term> rear, std::span<const Term> front) const {
        return reversed ? queue_of(context, front, rear) : queue_of(context, rear, front);
    }

    // The queue of one remaining list, rebalanced as OTP's r2f (a rear list) or f2r (a front list).
    Term rebalance(std::vector<Term> rest, bool rest_is_front) const {
        return rest_is_front != reversed ? from_front(context, std::move(rest)) : from_rear(context, std::move(rest));
    }
};

// delete(Item, Q1): the front-most item equal to Item removed; Q1 itself when absent.
Term remove_front(ProcessContext &context, const Term &item, const Queue &queue, bool reversed) {
    auto [rear, front] = lists_of(queue);
    if (reversed) {
        std::swap(rear, front);
    }
    const View view{context, reversed};
    if (auto f1 = without(front, item, false)) {
        return f1->empty() ? view.rebalance(rear, false) : view.build(rear, *f1);
    }
    if (auto r1 = without(rear, item, true)) {
        return r1->empty() ? view.rebalance(front, true) : view.build(*r1, front);
    }
    return queue.term;
}

Term delete_item(ProcessContext &context, const Element &item, const Queue &queue) {
    return remove_front(context, item.term, queue, false);
}

Term delete_r(ProcessContext &context, const Element &item, const Queue &queue) {
    return remove_front(context, item.term, queue, true);
}

constexpr std::array QUEUE_BUILTINS{
    typed_entry<to_list>("queue", "to_list"),   typed_entry<from_list>("queue", "from_list"),
    typed_entry<member>("queue", "member"),     typed_entry<in>("queue", "in"),
    typed_entry<in_r>("queue", "in_r"),         typed_entry<out>("queue", "out"),
    typed_entry<out_r>("queue", "out_r"),       typed_entry<get>("queue", "get"),
    typed_entry<get_r>("queue", "get_r"),       typed_entry<peek>("queue", "peek"),
    typed_entry<peek_r>("queue", "peek_r"),     typed_entry<drop>("queue", "drop"),
    typed_entry<drop_r>("queue", "drop_r"),     typed_entry<join>("queue", "join"),
    typed_entry<split>("queue", "split"),       typed_entry<delete_item>("queue", "delete"),
    typed_entry<delete_r>("queue", "delete_r"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> queue_builtins() noexcept { return builtins::collections::QUEUE_BUILTINS; }
} // namespace clause::runtime
