#include "collections.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <ranges>
#include <variant>

// The array builtins (plan 21 step LIB3B, docs/builtins.md#collections): OTP 29's #array{size, zero, fix, default,
// cache, cache_index, elements, bits} record, a tuple tree of 16-entry leaves ([] for an empty subtree) with one
// leaf cached and a zero offset that shift/2 and prepend/2 move. Every operation changes the record exactly as OTP's
// does, so arrays equal OTP's. A term that is not an array, or an index out of range, raises badarg.
namespace clause::runtime::builtins::collections {
namespace {
constexpr std::int64_t SHIFT = 4;
constexpr std::int64_t LEAF = 16;
constexpr std::int64_t MASK = LEAF - 1;

std::int64_t span_of(std::int64_t bits) { return std::int64_t{1} << bits; }

std::int64_t mask_of(std::int64_t bits) { return span_of(bits) - 1; }

// The roles of array arguments.
struct ArrayRole;
struct IndexRole;
using ArrayTerm = Named<ArrayRole>;
using Index = Named<IndexRole>;

// An index in a tree of `bits` (the tree's depth: 0 for a single leaf, 4 more per level of nodes).
struct At {
    std::int64_t index;
    std::int64_t bits;
};

// The low and high index of a fold, mapfold or values range.
struct Range {
    std::int64_t low;
    std::int64_t high;
};

// The fields of an array record.
struct Record {
    std::int64_t size;
    std::int64_t zero;
    Term fix;
    Term fallback;
    Term cache;
    std::int64_t cache_index;
    Term elements;
    std::int64_t bits;
};

bool is_empty(const Term &node) { return node.is_nil(); }

// The record of an array; none for anything else.
std::optional<Record> decode(const Term &term) {
    if (!term.is_tuple() || need(term.tuple_size()) != 9) {
        return std::nullopt;
    }
    const auto parts = need(term.tuple_elements());
    if (!parts[0].is_atom() || need(parts[0].atom_spelling()) != "array") {
        return std::nullopt;
    }
    const auto size = small(parts[1].word());
    const auto zero = small(parts[2].word());
    const auto cache_index = small(parts[6].word());
    const auto bits = small(parts[8].word());
    if (!size || !zero || !cache_index || !bits) {
        return std::nullopt;
    }
    return Record{*size, *zero, parts[3], parts[4], parts[5], *cache_index, parts[7], *bits};
}

Record record_of(const ArrayTerm &array) {
    const auto record = decode(array.term);
    if (!record) {
        bad_argument();
    }
    return *record;
}

std::int64_t integer_of(const Term &term) {
    const auto value = small(term.word());
    if (!value) {
        bad_argument();
    }
    return *value;
}

bool is_true(const Term &term) { return term.is_atom() && need(term.atom_spelling()) == "true"; }

// Tuple tree operations of one array (the default fills new leaves).
class Tree final {
  public:
    Tree(ProcessContext &context, const Term &fallback) : context_(&context), fallback_(fallback) {}

    Term encode(const Record &record) const {
        TermFactory factory(*context_);
        const auto integer = [&](std::int64_t value) { return need(factory.integer(value)); };
        const std::array parts{need(factory.atom("array")),
                               integer(record.size),
                               integer(record.zero),
                               record.fix,
                               record.fallback,
                               record.cache,
                               integer(record.cache_index),
                               record.elements,
                               integer(record.bits)};
        return need(factory.tuple(parts));
    }

    Term nil() const { return need(TermFactory(*context_).nil()); }

    Term fallback() const { return fallback_; }

    Term make(std::span<const Term> parts) const { return need(TermFactory(*context_).tuple(parts)); }

    // A node of 16 empty subtrees, and a leaf of 16 defaults.
    Term new_node() const { return filled(nil()); }

    Term new_leaf() const { return filled(fallback_); }

    Term filled(const Term &value) const {
        const std::vector<Term> parts(LEAF, value);
        return need(TermFactory(*context_).tuple(parts));
    }

    Term element(const Term &node, std::int64_t index) const {
        return need(node.tuple_element(static_cast<std::size_t>(index)));
    }

    Term with(const Term &node, std::int64_t index, const Term &value) const {
        auto parts = need(node.tuple_elements());
        parts.at(static_cast<std::size_t>(index)) = value;
        return need(TermFactory(*context_).tuple(parts));
    }

    // OTP's get_leaf: the leaf holding the index, a fresh leaf of defaults for an empty subtree.
    Term get_leaf(At at, const Term &node) const {
        auto current = node;
        for (auto level = at.bits;; level -= SHIFT) {
            if (is_empty(current)) {
                return new_leaf();
            }
            if (level == 0) {
                return current;
            }
            current = element(current, (at.index >> level) & MASK);
        }
    }

    // OTP's set_leaf: the tree with the leaf of the index replaced by `leaf`.
    Term set_leaf(At at, const Term &node, const Term &leaf) const {
        if (at.bits == 0) {
            return leaf;
        }
        const auto slot = (at.index >> at.bits) & MASK;
        const auto child = is_empty(node) ? nil() : element(node, slot);
        return with(is_empty(node) ? new_node() : node, slot, set_leaf({at.index, at.bits - SHIFT}, child, leaf));
    }

    // OTP's get_1: the value at the index read from the tree.
    Term value(At at, const Term &node) const {
        auto current = node;
        for (auto level = at.bits;; level -= SHIFT) {
            if (is_empty(current)) {
                return fallback_;
            }
            if (level == 0) {
                return element(current, at.index & MASK);
            }
            current = element(current, (at.index >> level) & MASK);
        }
    }

    // OTP's grow: the tree deepened (an empty one only gets more bits) until it holds index `i`.
    std::pair<Term, std::int64_t> grow(std::int64_t i, const Term &node, std::int64_t bits) const {
        if (is_empty(node)) {
            return {node, find_bits(i, bits)};
        }
        auto tree = node;
        while (i >= span_of(bits + SHIFT)) {
            tree = with(new_node(), 0, tree);
            bits += SHIFT;
        }
        return {tree, bits};
    }

    static std::int64_t find_bits(std::int64_t i, std::int64_t bits) {
        while (i >= span_of(bits + SHIFT)) {
            bits += SHIFT;
        }
        return bits;
    }

    // A leaf or node with the entries after position `n` (from 0) set to `value` (prune_right), or before it
    // (prune_left).
    Term prune(const Term &node, std::int64_t n, const Term &value, bool right) const {
        auto parts = need(node.tuple_elements());
        const auto last = static_cast<std::int64_t>(parts.size()) - 1;
        if (right ? n >= last : n <= 0) {
            return node;
        }
        for (std::int64_t at = 0; at <= last; ++at) {
            if (right ? at > n : at < n) {
                parts[static_cast<std::size_t>(at)] = value;
            }
        }
        return need(TermFactory(*context_).tuple(parts));
    }

    // OTP's shrink_right: the tree cut after index `i`, collapsed to fewer bits while index `i` fits the first child.
    std::pair<Term, std::int64_t> shrink_right(std::int64_t i, std::int64_t bits, const Term &node) const {
        auto tree = node;
        while (true) {
            if (is_empty(tree)) {
                return {tree, find_bits(i, 0)};
            }
            if (bits == 0) {
                return {prune(tree, i, fallback_, true), 0};
            }
            if (i >= span_of(bits)) {
                return {shrink_right_2(i, bits, tree), bits};
            }
            tree = element(tree, 0);
            bits -= SHIFT;
        }
    }

    Term shrink_right_2(std::int64_t i, std::int64_t bits, const Term &node) const {
        if (is_empty(node)) {
            return node;
        }
        if (bits == 0) {
            return prune(node, i, fallback_, true);
        }
        const auto at = i >> bits;
        const auto pruned = prune(node, at, nil(), true);
        return with(pruned, at, shrink_right_2(i & mask_of(bits), bits - SHIFT, element(pruned, at)));
    }

    Term shrink_left_2(std::int64_t z, std::int64_t bits, const Term &node) const {
        if (is_empty(node)) {
            return node;
        }
        if (bits == 0) {
            return prune(node, z, fallback_, false);
        }
        const auto at = z >> bits;
        const auto pruned = prune(node, at, nil(), false);
        return with(pruned, at, shrink_left_2(z & mask_of(bits), bits - SHIFT, element(pruned, at)));
    }

    // OTP's shrink_left: the entries before offset `z` dropped, then the tree cut after `z + max`; the new tree,
    // zero offset and bits.
    std::tuple<Term, std::int64_t, std::int64_t> shrink_left(std::int64_t z, std::int64_t max, std::int64_t bits,
                                                             const Term &node) const {
        auto tree = node;
        while (true) {
            if (is_empty(tree)) {
                return {tree, 0, find_bits(max, 0)};
            }
            if (bits == 0) {
                const auto [cut, cut_bits] = shrink_right(z + max, 0, prune(tree, z, fallback_, false));
                return {cut, z, cut_bits};
            }
            if ((z >> bits) != ((z + max) >> bits)) {
                const auto [cut, cut_bits] = shrink_right(z + max, bits, shrink_left_2(z, bits, tree));
                return {cut, z, cut_bits};
            }
            tree = element(tree, z >> bits);
            z &= mask_of(bits);
            bits -= SHIFT;
        }
    }

    // OTP's shrink: the tree, zero offset and bits of the entries from offset `z`, `size` of them.
    std::tuple<Term, std::int64_t, std::int64_t> shrink(std::int64_t z, std::int64_t size, std::int64_t bits,
                                                        const Term &node) const {
        if (z == 0 && size == 0) {
            return {nil(), 0, 0};
        }
        if (z > 0) {
            return shrink_left(z, size - 1, bits, node);
        }
        const auto [cut, cut_bits] = shrink_right(size - 1, bits, node);
        return {cut, 0, cut_bits};
    }

    // OTP's grow_left: a new root added on the left until the zero offset is not negative.
    std::tuple<Term, std::int64_t, std::int64_t> grow_left(std::int64_t z, const Term &node, std::int64_t bits) const {
        auto tree = node;
        while (z < 0) {
            bits += SHIFT;
            z += (LEAF / 2) * span_of(bits);
            if (!is_empty(tree)) {
                tree = with(new_node(), LEAF / 2, tree);
            }
        }
        return {tree, bits, z};
    }

  private:
    ProcessContext *context_;
    Term fallback_;
};

// new(Size, Fixed, Default) as OTP's new/3.
Record fresh(const Tree &tree, std::int64_t size, bool fixed, const Term &fallback, ProcessContext &context) {
    return {size,
            0,
            need(TermFactory(context).boolean(fixed)),
            fallback,
            tree.new_leaf(),
            0,
            tree.nil(),
            Tree::find_bits(size - 1, 0)};
}

// The settings new/1,2 collect from their options.
struct Options {
    std::int64_t size;
    bool fixed;
    Term fallback;
};

// The options as a list: a list itself, or one option.
std::vector<Term> option_list(const Term &options) {
    if (!options.is_list()) {
        return {options};
    }
    std::vector<Term> listed;
    auto rest = options;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        listed.push_back(need(rest.head()));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return listed;
}

// A non-negative size, or none.
std::optional<std::int64_t> natural(const Term &term) {
    const auto value = small(term.word());
    return value && *value >= 0 ? value : std::nullopt;
}

// One option of new/1,2: fixed, {fixed, Bool}, {default, Value}, {size, N} or N; badarg for anything else.
void apply_option(Options &settings, const Term &option) {
    if (option.is_atom() && need(option.atom_spelling()) == "fixed") {
        settings.fixed = true;
        return;
    }
    const auto parts = pair_of(option).value_or(std::pair{option, option});
    const auto key = pair_of(option) && parts.first.is_atom() ? need(parts.first.atom_spelling()) : std::string_view{};
    const auto sized = key == "size" ? natural(parts.second) : natural(option);
    if (key == "fixed" && parts.second.is_boolean()) {
        settings.fixed = is_true(parts.second);
    } else if (key == "default") {
        settings.fallback = parts.second;
    } else if (sized) {
        settings.size = *sized;
        settings.fixed = true;
    } else {
        bad_argument();
    }
}

// new/1,2 from their options, starting from `size` and `fixed`.
Term from_options(ProcessContext &context, const Term &options, std::int64_t size, bool fixed) {
    Options settings{size, fixed, need(TermFactory(context).atom("undefined"))};
    for (const auto &option : option_list(options)) {
        apply_option(settings, option);
    }
    const Tree tree(context, settings.fallback);
    return tree.encode(fresh(tree, settings.size, settings.fixed, settings.fallback, context));
}

Term new0(ProcessContext &context) { return from_options(context, need(TermFactory(context).nil()), 0, false); }

Term new1(ProcessContext &context, const List &options) { return from_options(context, options.term, 0, false); }

Term new2(ProcessContext &context, const Index &size, const List &options) {
    const auto n = small(size.term.word());
    if (!n || *n < 0) {
        bad_argument();
    }
    return from_options(context, options.term, *n, true);
}

// get(I, Array).
Term get(ProcessContext &context, const Index &index, const ArrayTerm &array) {
    const auto record = record_of(array);
    const auto i0 = integer_of(index.term);
    if (i0 < 0) {
        bad_argument();
    }
    if (i0 >= record.size) {
        if (is_true(record.fix)) {
            bad_argument();
        }
        return record.fallback;
    }
    const auto i = i0 + record.zero;
    const Tree tree(context, record.fallback);
    if (i >= record.cache_index && i < record.cache_index + LEAF) {
        return tree.element(record.cache, i - record.cache_index);
    }
    return tree.value({i, record.bits}, record.elements);
}

// The array with its cached leaf written back and leaf `ci` cached instead, value `value` at offset `r` in it.
void recache(const Tree &tree, Record &record, std::int64_t ci, std::int64_t r, const Value &value) {
    const auto written = tree.set_leaf({record.cache_index, record.bits}, record.elements, record.cache);
    record.elements = written;
    record.cache = tree.with(tree.get_leaf({ci, record.bits}, written), r, value.term);
    record.cache_index = ci;
}

// set(I, Value, Array): a fixed array's index past its size is badarg; another grows.
Term set_value(ProcessContext &context, const Index &index, const Value &value, const ArrayTerm &array) {
    auto record = record_of(array);
    const auto i0 = integer_of(index.term);
    if (i0 < 0) {
        bad_argument();
    }
    const Tree tree(context, record.fallback);
    const auto i = i0 + record.zero;
    const auto r = i & MASK;
    const auto ci = i - r;
    if (i0 < record.size) {
        if (i >= record.cache_index && i < record.cache_index + LEAF) {
            record.cache = tree.with(record.cache, i - record.cache_index, value.term);
        } else {
            recache(tree, record, ci, r, value);
        }
        return tree.encode(record);
    }
    if (is_true(record.fix)) {
        bad_argument();
    }
    record.size = i0 + 1;
    if (i >= span_of(record.bits + SHIFT)) {
        const auto [grown, bits] = tree.grow(i, record.elements, record.bits);
        record.elements = grown;
        record.bits = bits;
        recache(tree, record, ci, r, value);
    } else if (ci != record.cache_index) {
        recache(tree, record, ci, r, value);
    } else {
        record.cache = tree.with(record.cache, r, value.term);
    }
    return tree.encode(record);
}

// append(Value, Array): the value at index size(Array).
Term append_value(ProcessContext &context, const Value &value, const ArrayTerm &array) {
    auto record = record_of(array);
    const Tree tree(context, record.fallback);
    const auto i = record.size + record.zero;
    const auto r = i & MASK;
    const auto ci = i - r;
    record.size += 1;
    if (i < record.cache_index + LEAF) {
        record.cache = tree.with(record.cache, i - record.cache_index, value.term);
    } else if (i < span_of(record.bits + SHIFT)) {
        recache(tree, record, ci, r, value);
    } else {
        const auto [grown, bits] = tree.grow(i, record.elements, record.bits);
        record.elements = grown;
        record.bits = bits;
        recache(tree, record, ci, r, value);
    }
    return tree.encode(record);
}

// shift(Steps, Array): the zero offset moved; a negative one grows the tree on the left.
Record shifted(const Tree &tree, Record record, std::int64_t steps) {
    if (steps > record.size) {
        bad_argument();
    }
    const auto z1 = record.zero + steps;
    record.size -= steps;
    if (z1 >= 0) {
        record.zero = z1;
        return record;
    }
    const auto [grown, bits, z2] = tree.grow_left(z1, record.elements, record.bits);
    record.cache_index += z2 - z1;
    record.zero = z2;
    record.elements = grown;
    record.bits = bits;
    return record;
}

Term shift(ProcessContext &context, const Index &steps, const ArrayTerm &array) {
    const auto record = record_of(array);
    const auto count = integer_of(steps.term);
    const Tree tree(context, record.fallback);
    return count == 0 ? array.term : tree.encode(shifted(tree, record, count));
}

// prepend(Value, Array): function_clause for a non-array.
Term prepend(ProcessContext &context, const Value &value, const ArrayTerm &array) {
    if (!decode(array.term)) {
        no_clause();
    }
    const auto record = record_of(array);
    const Tree tree(context, record.fallback);
    return set_value(context, {need(TermFactory(context).integer(0))}, value, {tree.encode(shifted(tree, record, -1))});
}

// slice(I, Length, Array): the entries I..I+Length-1 as an array.
Term slice(ProcessContext &context, const Index &start, const Value &length, const ArrayTerm &array) {
    const auto record = record_of(array);
    const auto i = integer_of(start.term);
    const auto n = integer_of(length.term);
    if (i < 0 || n < 0 || record.size < 0 || i + n > record.size) {
        bad_argument();
    }
    const Tree tree(context, record.fallback);
    auto result = i == 0 ? record : shifted(tree, record, i);
    result.size = n;
    return tree.encode(result);
}

// reset(I, Array): the entry back to the default; unchanged when already the default in the tree.
Term reset(ProcessContext &context, const Index &index, const ArrayTerm &array) {
    auto record = record_of(array);
    const auto i0 = integer_of(index.term);
    if (i0 < 0) {
        bad_argument();
    }
    if (i0 >= record.size) {
        if (is_true(record.fix)) {
            bad_argument();
        }
        return array.term;
    }
    const Tree tree(context, record.fallback);
    const auto i = i0 + record.zero;
    if (i >= record.cache_index && i < record.cache_index + LEAF) {
        record.cache = tree.with(record.cache, i - record.cache_index, record.fallback);
        return tree.encode(record);
    }
    if (same(tree.value({i, record.bits}, record.elements), record.fallback)) {
        return array.term;
    }
    auto leaf = tree.get_leaf({i, record.bits}, record.elements);
    leaf = tree.with(leaf, i & MASK, record.fallback);
    record.elements = tree.set_leaf({i, record.bits}, record.elements, leaf);
    return tree.encode(record);
}

// resize(Size, Array).
Term resize2(ProcessContext &context, const Index &size, const ArrayTerm &array) {
    auto record = record_of(array);
    const auto n = integer_of(size.term);
    if (n < 0 || record.size < 0) {
        bad_argument();
    }
    const Tree tree(context, record.fallback);
    if (n > record.size && record.zero == 0) {
        const auto [grown, bits] = tree.grow(record.zero + n - 1, record.elements, record.bits);
        record.size = n;
        record.elements = grown;
        record.bits = bits;
        return tree.encode(record);
    }
    if (n == record.size && record.zero == 0) {
        return array.term;
    }
    const auto written = tree.set_leaf({record.cache_index, record.bits}, record.elements, record.cache);
    const auto keep = n > record.size ? record.size : n;
    auto [cut, zero, bits] = tree.shrink(record.zero, keep, record.bits, written);
    if (n > record.size) {
        std::tie(cut, bits) = tree.grow(zero + n - 1, cut, bits);
    }
    record.size = n;
    record.zero = zero;
    record.elements = cut;
    record.bits = bits;
    record.cache_index = 0;
    record.cache = tree.get_leaf({0, bits}, cut);
    return tree.encode(record);
}

// Every value from index `low` to `high` in order.
std::vector<Term> values_of(ProcessContext &context, const Record &record, Range range) {
    const Tree tree(context, record.fallback);
    const auto elements = tree.set_leaf({record.cache_index, record.bits}, record.elements, record.cache);
    std::vector<Term> values;
    for (auto i = range.low; i <= range.high; ++i) {
        values.push_back(tree.value({i + record.zero, record.bits}, elements));
    }
    return values;
}

// The highest index whose value is not the default, plus one; 0 when none.
std::int64_t used_size(ProcessContext &context, const Record &record) {
    const auto values = values_of(context, record, {0, record.size - 1});
    const auto last = std::ranges::find_if(values | std::views::reverse,
                                           [&](const Term &value) { return !same(value, record.fallback); });
    return static_cast<std::int64_t>(std::ranges::distance(last, (values | std::views::reverse).end()));
}

Term sparse_size(ProcessContext &context, const ArrayTerm &array) {
    return need(TermFactory(context).integer(used_size(context, record_of(array))));
}

Term resize1(ProcessContext &context, const ArrayTerm &array) {
    const auto size = sparse_size(context, array);
    return resize2(context, {size}, array);
}

// to_list/1, sparse_to_list/1, to_orddict/1, sparse_to_orddict/1.
Term listed(ProcessContext &context, const ArrayTerm &array, bool sparse, bool indexed) {
    const auto record = record_of(array);
    Prefix result;
    std::int64_t i = 0;
    for (const auto &value : values_of(context, record, {0, record.size - 1})) {
        if (!sparse || !same(value, record.fallback)) {
            const std::array pair{need(TermFactory(context).integer(i)), value};
            result.push(indexed ? need(TermFactory(context).tuple(pair)) : value);
        }
        ++i;
    }
    return result.done(context);
}

Term to_list(ProcessContext &context, const ArrayTerm &array) { return listed(context, array, false, false); }

Term sparse_to_list(ProcessContext &context, const ArrayTerm &array) { return listed(context, array, true, false); }

Term to_orddict(ProcessContext &context, const ArrayTerm &array) { return listed(context, array, false, true); }

Term sparse_to_orddict(ProcessContext &context, const ArrayTerm &array) { return listed(context, array, true, true); }

Term from_values_list(ProcessContext &context, const Term &list, const Value &fallback, bool empty_is_new);

Term from_list2(ProcessContext &context, const List &list, const Value &fallback) {
    return from_values_list(context, list.term, fallback, true);
}

Term from_list1(ProcessContext &context, const List &list) {
    return from_list2(context, list, {need(TermFactory(context).atom("undefined"))});
}

// clause_array:from_values(Values, Default): from_list/2's array also for no values (array:from/2,3).
Term from_values(ProcessContext &context, const List &list, const Value &fallback) {
    return from_values_list(context, list.term, fallback, false);
}

Term from_values_list(ProcessContext &context, const Term &list, const Value &fallback, bool empty_is_new) {
    if (!list.is_list()) {
        bad_argument();
    }
    Tree tree(context, fallback.term);
    if (list.is_nil() && empty_is_new) {
        return tree.encode(fresh(tree, 0, false, fallback.term, context));
    }
    std::vector<Term> values;
    auto rest = list;
    for (; rest.is_cons(); rest = need(rest.tail())) {
        values.push_back(need(rest.head()));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    const auto count = static_cast<std::int64_t>(values.size());
    std::vector<Term> leaves;
    for (std::size_t at = 0; at == 0 || at < values.size(); at += LEAF) {
        std::vector<Term> leaf(LEAF, fallback.term);
        std::copy_n(values.begin() + static_cast<std::ptrdiff_t>(at),
                    std::min<std::size_t>(LEAF, values.size() - std::min(at, values.size())), leaf.begin());
        leaves.push_back(need(TermFactory(context).tuple(leaf)));
    }
    std::int64_t bits = SHIFT;
    while (leaves.size() > 1) {
        const auto children = static_cast<std::size_t>(((count - 1) >> bits) + 1);
        leaves.resize(((children + LEAF - 1) / LEAF) * LEAF, tree.nil());
        std::vector<Term> nodes;
        for (std::size_t at = 0; at < leaves.size(); at += LEAF) {
            nodes.push_back(need(TermFactory(context).tuple(std::span(leaves).subspan(at, LEAF))));
        }
        leaves = std::move(nodes);
        bits += SHIFT;
    }
    const auto root = leaves.front();
    const auto root_bits = bits - SHIFT;
    const Record record{count,
                        0,
                        need(TermFactory(context).boolean(false)),
                        fallback.term,
                        tree.get_leaf({0, root_bits}, root),
                        0,
                        root,
                        root_bits};
    return tree.encode(record);
}

// A piece of from_orddict's list of leaves: a leaf or node, or a gap of `gap` indices of empty subtrees.
struct Piece {
    std::optional<Term> node;
    std::int64_t gap = 0;
};

using Pieces = std::deque<Piece>;

// error({badarg, Rest}), from_orddict's error for an entry out of order or of the wrong shape.
[[noreturn]] void bad_orddict(ProcessContext &context, const Term &rest) {
    const std::array parts{need(TermFactory(context).atom("badarg")), rest};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                         .payload = need(TermFactory(context).tuple(parts)).word()};
}

// The state of one level of OTP's collect_leafs: the children of the node being filled (in the order taken, the
// highest first) and how many it still takes, the nodes made (the last first) and the pieces left.
struct Level {
    Pieces pieces;
    std::vector<Term> taken;
    std::int64_t room = LEAF;
    Pieces made;
};

// One piece taken into the node being filled at `bits`.
void take_piece(const Tree &tree, Level &level, std::int64_t bits) {
    auto piece = level.pieces.front();
    level.pieces.pop_front();
    if (piece.node) {
        level.taken.push_back(*piece.node);
        --level.room;
        return;
    }
    const auto children = piece.gap >> bits;
    const auto empties = [&](std::int64_t count) {
        level.taken.insert(level.taken.end(), static_cast<std::size_t>(count), tree.nil());
    };
    if (children < level.room) {
        empties(children);
        level.room -= children;
    } else if (level.room == LEAF) {
        empties(children % LEAF);
        level.room -= children % LEAF;
        level.made.push_front(piece);
    } else {
        empties(level.room);
        if (children > level.room) {
            level.pieces.push_front({std::nullopt, (children - level.room) << bits});
        }
        level.room = 0;
    }
}

// The pieces of one level of collect_leafs, padded on the right as OTP pads them.
Pieces padded(Pieces pieces, std::int64_t count, std::int64_t bits) {
    const auto children = ((count - 1) >> bits) + 1;
    const auto pad = ((LEAF - children % LEAF) % LEAF) * span_of(bits);
    if (pad != 0) {
        pieces.push_front({std::nullopt, pad});
    }
    return pieces;
}

// The node filled at one level: the root when nothing else is left, else kept for the next level.
std::optional<Term> finish_node(const Tree &tree, Level &level) {
    std::ranges::reverse(level.taken);
    auto node = tree.make(level.taken);
    if (level.pieces.empty() && level.made.empty()) {
        return node;
    }
    level.made.push_front({node, 0});
    level.taken.clear();
    level.room = LEAF;
    return std::nullopt;
}

// OTP's collect_leafs: the pieces (the highest first) gathered into nodes level by level up to the root; the root
// and its bits extended once more.
std::pair<Term, std::int64_t> collect(const Tree &tree, std::int64_t count, Pieces pieces, std::int64_t bits) {
    while (true) {
        Level level{padded(std::move(pieces), count, bits), {}, LEAF, {}};
        while (level.room == 0 || !level.pieces.empty()) {
            if (level.room != 0) {
                take_piece(tree, level, bits);
            } else if (const auto root = finish_node(tree, level)) {
                return {*root, bits + SHIFT};
            }
        }
        pieces = Pieces(level.made.rbegin(), level.made.rend());
        bits += SHIFT;
    }
}

// One leaf of from_orddict from index `start`: its values (the default in holes and after the end) and the size
// so far, and the entries left. A key out of order, or an entry that is not {Index, Value}, raises {badarg, Rest}.
// The value of position `at` from the head of the entries left, taking the entry when its index is `at`; false
// after the end of the entries (the default without counting toward the size).
bool orddict_value(ProcessContext &context, const Tree &tree, Term &rest, std::int64_t at, std::vector<Term> &leaf) {
    if (!rest.is_cons()) {
        leaf.push_back(tree.fallback());
        return false;
    }
    const auto parts = pair_of(need(rest.head()));
    const auto key = parts ? small(parts->first.word()) : std::nullopt;
    if (!parts || !key || *key < at) {
        bad_orddict(context, rest);
    }
    leaf.push_back(*key == at ? parts->second : tree.fallback());
    rest = *key == at ? need(rest.tail()) : rest;
    return true;
}

std::tuple<Term, std::int64_t, Term> orddict_leaf(ProcessContext &context, const Tree &tree, Term rest,
                                                  std::int64_t start) {
    std::vector<Term> leaf;
    auto size = start;
    for (auto at = start; at < start + LEAF; ++at) {
        if (orddict_value(context, tree, rest, at, leaf)) {
            size = at + 1;
        }
    }
    return {tree.make(leaf), size, rest};
}

// OTP's from_orddict_0: the leaves (the last first) and gap markers of an orddict, and its size.
std::pair<Pieces, std::int64_t> orddict_pieces(ProcessContext &context, const Tree &tree, Term rest) {
    Pieces pieces;
    std::int64_t index = 0;
    while (!rest.is_nil()) {
        const auto parts = rest.is_cons() ? pair_of(need(rest.head())) : std::nullopt;
        if (!parts) {
            bad_orddict(context, rest);
        }
        const auto key = small(parts->first.word());
        if (key && *key > index + LEAF) {
            const auto step = (*key - index) - ((*key - index) & MASK);
            pieces.push_front({std::nullopt, step});
            index += step;
            continue;
        }
        auto [leaf, size, left] = orddict_leaf(context, tree, rest, index);
        pieces.push_front({leaf, 0});
        rest = left;
        index = size;
    }
    return {std::move(pieces), index};
}

// from_orddict(List, Default): the {Index, Value} pairs of strictly increasing indices; gaps of whole leaves stay
// empty subtrees.
Term from_orddict2(ProcessContext &context, const List &list, const Value &fallback) {
    if (!list.term.is_list()) {
        bad_argument();
    }
    const Tree tree(context, fallback.term);
    if (list.term.is_nil()) {
        return tree.encode(fresh(tree, 0, false, fallback.term, context));
    }
    auto [pieces, index] = orddict_pieces(context, tree, list.term);
    auto root = pieces.front().node.value_or(tree.nil());
    std::int64_t root_bits = 0;
    if (pieces.size() != 1 || !pieces.front().node) {
        std::tie(root, root_bits) = collect(tree, index, std::move(pieces), SHIFT);
        root_bits -= SHIFT;
    }
    const Record record{index,
                        0,
                        need(TermFactory(context).boolean(false)),
                        fallback.term,
                        tree.get_leaf({0, root_bits}, root),
                        0,
                        root,
                        root_bits};
    return tree.encode(record);
}

Term from_orddict1(ProcessContext &context, const List &list) {
    return from_orddict2(context, list, {need(TermFactory(context).atom("undefined"))});
}

// The record and the index range Low..High of a fold or mapfold; badarg unless Low >= 0 and High < size.
std::pair<Record, Range> ranged(const ArrayTerm &array, const Index &low, const Value &high) {
    const auto record = record_of(array);
    const auto from = small(low.term.word());
    const auto to = small(high.term.word());
    if (!from || !to || *from < 0 || *to >= record.size) {
        bad_argument();
    }
    return {record, {*from, *to}};
}

// clause_array:values(Array, Low, High): the values of Low..High in order (array folds and mapfolds).
Term values(ProcessContext &context, const ArrayTerm &array, const Index &low, const Value &high) {
    const auto [record, range] = ranged(array, low, high);
    return need(TermFactory(context).list(values_of(context, record, range)));
}

// clause_array:sparse(Array, Low, High): the {Index, Value} pairs of Low..High whose value is not the default.
Term sparse(ProcessContext &context, const ArrayTerm &array, const Index &low, const Value &high) {
    const auto [record, range] = ranged(array, low, high);
    Prefix pairs;
    auto i = range.low;
    for (const auto &value : values_of(context, record, range)) {
        if (!same(value, record.fallback)) {
            const std::array pair{need(TermFactory(context).integer(i)), value};
            pairs.push(need(TermFactory(context).tuple(pair)));
        }
        ++i;
    }
    return pairs.done(context);
}

// The new values of a mapfold, consumed in index order; a sparse map leaves the defaults and empty subtrees alone.
struct Mapped {
    Term rest;
    bool sparse;

    Term next() {
        auto value = need(rest.head());
        rest = need(rest.tail());
        return value;
    }
};

// A leaf with its entries low..high replaced in order (a sparse map leaves the defaults alone).
Term map_leaf(const Tree &tree, const Term &leaf, Range range, Mapped &mapped) {
    auto parts = need(leaf.tuple_elements());
    for (auto at = range.low; at <= range.high; ++at) {
        auto &entry = parts[static_cast<std::size_t>(at)];
        entry = mapped.sparse && same(entry, tree.fallback()) ? entry : mapped.next();
    }
    return tree.make(parts);
}

// The part of `range` (offsets in a node of `bits`) inside child `at.index`, in the child's offsets.
Range child_range(Range range, At at) {
    const auto first = range.low >> at.bits;
    const auto last = range.high >> at.bits;
    return {at.index == first ? range.low & mask_of(at.bits) : 0,
            at.index == last ? range.high & mask_of(at.bits) : span_of(at.bits) - 1};
}

Term map_range(const Tree &tree, const Term &node, std::int64_t bits, Range range, Mapped &mapped);

// OTP's mapfoldl_1 and sparse_mapfoldl_1 on the subtree `node` of `bits` for its offsets low..high: empty subtrees
// unfolded (not by a sparse map), leaf entries replaced in order.
Term map_range(const Tree &tree, const Term &node, std::int64_t bits, Range range, Mapped &mapped) {
    if (is_empty(node) && mapped.sparse) {
        return node;
    }
    if (bits == 0) {
        return map_leaf(tree, is_empty(node) ? tree.new_leaf() : node, range, mapped);
    }
    auto parts = need((is_empty(node) ? tree.new_node() : node).tuple_elements());
    const auto first = range.low >> bits;
    const auto last = range.high >> bits;
    for (auto child = first; child <= last; ++child) {
        auto &entry = parts[static_cast<std::size_t>(child)];
        entry = map_range(tree, entry, bits - SHIFT, child_range(range, {child, bits}), mapped);
    }
    return tree.make(parts);
}

// clause_array:replace(Array, {Low, High}, Values, Sparse): the mapfold's array, OTP's tree with the new values and
// its cache read again.
Term replace(ProcessContext &context, const ArrayTerm &array, const Value &range, const List &values,
             const Element &sparse) {
    auto record = record_of(array);
    const auto bounds = pair_of(range.term);
    if (!bounds) {
        bad_argument();
    }
    const Range span{integer_of(bounds->first), integer_of(bounds->second)};
    const Tree tree(context, record.fallback);
    const auto written = tree.set_leaf({record.cache_index, record.bits}, record.elements, record.cache);
    Mapped mapped{values.term, is_true(sparse.term)};
    const auto mapped_tree =
        map_range(tree, written, record.bits, {span.low + record.zero, span.high + record.zero}, mapped);
    record.elements = mapped_tree;
    record.cache = tree.get_leaf({record.cache_index, record.bits}, mapped_tree);
    return tree.encode(record);
}

constexpr std::array ARRAY_BUILTINS{
    typed_entry<new0>("array", "new"),
    typed_entry<new1>("array", "new"),
    typed_entry<new2>("array", "new"),
    typed_entry<get>("array", "get"),
    typed_entry<set_value>("array", "set"),
    typed_entry<append_value>("array", "append"),
    typed_entry<prepend>("array", "prepend"),
    typed_entry<shift>("array", "shift"),
    typed_entry<slice>("array", "slice"),
    typed_entry<reset>("array", "reset"),
    typed_entry<resize1>("array", "resize"),
    typed_entry<resize2>("array", "resize"),
    typed_entry<sparse_size>("array", "sparse_size"),
    typed_entry<to_list>("array", "to_list"),
    typed_entry<sparse_to_list>("array", "sparse_to_list"),
    typed_entry<to_orddict>("array", "to_orddict"),
    typed_entry<sparse_to_orddict>("array", "sparse_to_orddict"),
    typed_entry<from_list1>("array", "from_list"),
    typed_entry<from_list2>("array", "from_list"),
    typed_entry<from_orddict1>("array", "from_orddict"),
    typed_entry<from_orddict2>("array", "from_orddict"),
    typed_entry<from_values>("clause_array", "from_values"),
    typed_entry<values>("clause_array", "values"),
    typed_entry<sparse>("clause_array", "sparse"),
    typed_entry<replace>("clause_array", "replace"),
};
} // namespace
} // namespace clause::runtime::builtins::collections

namespace clause::runtime {
std::span<const BuiltinEntry> array_builtins() noexcept { return builtins::collections::ARRAY_BUILTINS; }
} // namespace clause::runtime
