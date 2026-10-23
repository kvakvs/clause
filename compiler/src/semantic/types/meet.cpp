#include "decimal.hpp"
#include "lattice.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <set>
#include <span>

// Narrowing of facts (docs/semantic.md#inference-domain): the meet of two facts is computed member by member; two
// members of different families share no value, and within a family each kind of fact keeps what both hold. The
// meet may hold more values than both facts share, but none() only when they share none.
namespace clause::semantic::types {
namespace {
// The largest character code, as in a string's elements.
constexpr std::string_view CHAR_LIMIT = "1114111";
// Family ranks (Lattice::family) of numbers, atoms, funs, tuples, maps, the empty list, lists, bitstrings and other
// facts.
constexpr int NUMBERS = 0;
constexpr int ATOMS = 1;
constexpr int FUNS = 3;
constexpr int TUPLES = 6;
constexpr int MAPS = 7;
constexpr int NIL = 8;
constexpr int LISTS = 9;
constexpr int BITSTRINGS = 10;
constexpr int OTHER = 11;

// One member of a list fact: the empty list, a proper list, an improper list or any list.
struct ListShape {
    // `nonempty`: any list but the empty one.
    enum class Kind : std::uint8_t { nil, proper, improper, any, nonempty };
    Kind kind;
    // A proper list's element or an improper list's head, and an improper list's tail.
    Id head;
    Id tail;
    bool nonempty = false;
};

// A bitstring member's sizes: `base` bits plus any multiple of `unit`.
struct Sizes {
    std::uint64_t base = 0;
    std::uint64_t unit = 0;
};

// Whether every size of `inner` is a size of `outer`.
bool sizes_within(const Sizes inner, const Sizes outer) {
    if (inner.base < outer.base) {
        return false;
    }
    if (outer.unit == 0) {
        return inner.base == outer.base && inner.unit == 0;
    }
    return (inner.base - outer.base) % outer.unit == 0 && inner.unit % outer.unit == 0;
}

// The larger of two lower bounds and the smaller of two upper bounds; a missing bound is unbounded.
std::optional<std::string> higher(const std::optional<std::string> &left, const std::optional<std::string> &right) {
    if (!left || !right) {
        return left ? left : right;
    }
    return decimal_less(*left, *right) ? right : left;
}

std::optional<std::string> lower(const std::optional<std::string> &left, const std::optional<std::string> &right) {
    if (!left || !right) {
        return left ? left : right;
    }
    return decimal_less(*left, *right) ? left : right;
}

// Computes meets and containment of members of one family.
class Meet final {
  public:
    explicit Meet(Lattice &lattice) : lattice_(lattice), graph_(lattice.graph()) {}

    // The meet of two members (no unions).
    Id members(Id left, Id right);
    // Whether every value of member `inner` is surely a value of `outer`.
    bool within(Id inner, Id outer);

    // `within` by family: numbers and atoms that meet as themselves, any fun, tuple, map or list, nested sizes.
    bool same_meet(Id inner, Id outer) { return numbers(inner, outer) == inner; }

    // Whether the atoms of `inner` are atoms of `outer`, without building their meet. Added for the atom inclusion
    // without a meet (OPTC.A2).
    bool same_atoms(Id inner, Id outer) {
        if (inner == outer) {
            return true;
        }
        const auto a = atom_views(inner);
        const auto b = atom_views(outer);
        if (!a || !b) {
            return a.has_value();
        }
        return std::ranges::includes(*b, *a);
    }

    // The sorted atom names of an atom or boolean() fact, read in place; none for any other atom fact. Added for
    // fewer allocations (OPTC.C).
    std::optional<std::span<const std::string>> atom_views(Id fact) const {
        static const std::array<std::string, 2> BOOLEANS{"false", "true"};
        const auto &node = graph_.get(fact);
        if (node.kind == Kind::atom) {
            return std::span(&node.name, 1);
        }
        return node.name == "boolean" ? std::optional{std::span<const std::string>(BOOLEANS)} : std::nullopt;
    }

    bool any_fun(Id, Id outer) { return any(outer, "fun"); }

    bool any_tuple(Id inner, Id outer) { return any(outer, "tuple") || elements_within(inner, outer); }

    // Tuples of one size whose every element is within the other's element at its position.
    bool elements_within(Id inner, Id outer);
    // Whether every member of fact `inner` is within some member of fact `outer`.
    bool contained(Id inner, Id outer);

    bool any_map(Id, Id outer) { return any(outer, "map"); }

    bool any_list(Id inner, Id outer) {
        if (inner == outer) {
            return true;
        }
        const auto kind = list_shape(outer).kind;
        const auto shape = list_shape(inner);
        if (graph_.get(inner).kind == Kind::positional) {
            return kind == ListShape::Kind::any || kind == ListShape::Kind::nonempty;
        }
        return kind == ListShape::Kind::any ||
               (kind == ListShape::Kind::nonempty && (shape.nonempty || shape.kind == ListShape::Kind::improper));
    }

    bool fewer_sizes(Id inner, Id outer) { return sizes_within(sizes(inner), sizes(outer)); }

  private:
    // A family rank, the empty list counted with lists.
    int family(Id fact) const {
        const auto rank = lattice_.family(fact);
        return rank == NIL ? LISTS : rank;
    }

    // The meet of members that are equal, top, bottom, of a declared type or of different families; none for
    // members of one family.
    std::optional<Id> trivial(Id left, Id right);
    std::optional<Id> across(Id left, Id right);
    Id numbers(Id left, Id right);
    Id atoms(Id left, Id right);
    Id funs(Id left, Id right);
    // Whether a fun fact has one function type of any inputs: the funs of today.
    bool plain(Id fun) const;
    // The function types of `fun` of the arity of plain fun `plain`, their results met with its result.
    Id restricted(Id fun, Id plain);
    Id tuples(Id left, Id right);
    Id maps(Id left, Id right);
    // The meet of two exact maps: the same keys with values that meet.
    Id exact_maps(Id left, Id right);
    // The meet of two maps one of which is an association.
    Id associated(Id left, Id right);
    Id lists(Id left, Id right);
    Id bitstrings(Id left, Id right);
    // The meet of list shapes `a` and `b` (of `facts`) when one is any list or any nonempty list; none otherwise.
    std::optional<Id> loose(const ListShape &a, const ListShape &b, const std::pair<Id, Id> &facts);
    // The meet of a positional list with another list fact.
    Id positional(Id fact, Id other);
    // The meet of any nonempty list with `other` (the shape of `fact`).
    Id nonempty(const ListShape &other, Id fact);
    // The meet of two proper lists, of a proper list and the empty list, and of two improper lists.
    Id proper(const ListShape &left, const ListShape &right);

    // The atoms a member holds; none for any atom.
    std::optional<std::set<std::string>> atom_names(Id fact) const;
    // Whether a member is the category of its whole family (tuple(), map(), fun()).
    bool any(Id fact, std::string_view category) const;
    ListShape list_shape(Id fact);
    Sizes sizes(Id fact) const;

    Lattice &lattice_;
    Graph &graph_;
};

std::optional<Id> Meet::trivial(const Id left, const Id right) {
    if (left == right || right == graph_.top() || left == graph_.top()) {
        return left == graph_.top() ? right : left;
    }
    if (left == graph_.bottom() || right == graph_.bottom()) {
        return graph_.bottom();
    }
    return across(left, right);
}

std::optional<Id> Meet::across(const Id left, const Id right) {
    // A declared type is no fact of inference: keep the other side, which holds the shared values.
    if (family(left) == OTHER || family(right) == OTHER) {
        return family(left) == OTHER ? right : left;
    }
    return family(left) == family(right) ? std::nullopt : std::optional{graph_.bottom()};
}

Id Meet::members(const Id left, const Id right) {
    if (const auto result = trivial(left, right)) {
        return *result;
    }
    using Rule = Id (Meet::*)(Id, Id);
    static const std::map<int, Rule> RULES{{NUMBERS, &Meet::numbers},      {ATOMS, &Meet::atoms}, {FUNS, &Meet::funs},
                                           {TUPLES, &Meet::tuples},        {MAPS, &Meet::maps},   {LISTS, &Meet::lists},
                                           {BITSTRINGS, &Meet::bitstrings}};
    const auto rule = RULES.find(family(left));
    // Identifiers of one family (pid(), port(), reference()) are the same category.
    return rule == RULES.end() ? left : (this->*rule->second)(left, right);
}

Id Meet::numbers(const Id left, const Id right) {
    if (left == right) {
        return left;
    }
    const auto a = lattice_.numbers(left);
    const auto b = lattice_.numbers(right);
    std::vector<Id> results;
    if (a.integers && b.integers) {
        const auto low = higher(a.low, b.low);
        const auto high = lower(a.high, b.high);
        if (!low || !high || !decimal_less(*high, *low)) {
            results.push_back(lattice_.interval(low, high));
        }
    }
    if (a.floats && b.floats) {
        results.push_back(lattice_.category("float"));
    }
    return lattice_.join(results, 0);
}

std::optional<std::set<std::string>> Meet::atom_names(const Id fact) const {
    const auto &node = graph_.get(fact);
    if (node.kind == Kind::atom) {
        return std::set<std::string>{node.name};
    }
    return node.name == "boolean" ? std::optional{std::set<std::string>{"false", "true"}} : std::nullopt;
}

Id Meet::atoms(const Id left, const Id right) {
    const auto a = atom_names(left);
    const auto b = atom_names(right);
    if (!a || !b) {
        return a ? left : right;
    }
    std::vector<Id> results;
    for (const auto &name : *a) {
        if (b->contains(name)) {
            results.push_back(lattice_.atom(name));
        }
    }
    return lattice_.join(results, 0);
}

bool Meet::any(const Id fact, const std::string_view category) const {
    const auto &node = graph_.get(fact);
    if (category == "fun") {
        return !std::ranges::contains(node.labels, std::string("result"));
    }
    return node.name == "any" || (node.kind == Kind::application && node.name == category);
}

Id Meet::funs(const Id left, const Id right) {
    if (any(left, "fun") || any(right, "fun")) {
        return any(left, "fun") ? right : left;
    }
    if (plain(right) || plain(left)) {
        return plain(right) ? restricted(left, right) : restricted(right, left);
    }
    // Different funs meet as the funs of today: their function types and inputs are given up.
    const auto a = graph_.get(lattice_.joined_fun(left)).children;
    const auto b = graph_.get(lattice_.joined_fun(right)).children;
    return a.size() != b.size() ? graph_.bottom() : lattice_.fun(a.size() - 1, lattice_.meet(a.back(), b.back()));
}

bool Meet::plain(const Id fun) const {
    const auto &node = graph_.get(fun);
    return node.name == "product" &&
           std::all_of(node.children.begin(), node.children.end() - 1, [&](Id input) { return input == graph_.top(); });
}

Id Meet::restricted(const Id fun, const Id plain) {
    if (fun == plain) {
        return fun;
    }
    const auto bound = graph_.get(plain).children;
    std::vector<Id> types;
    for (const auto type : lattice_.function_types(fun)) {
        const auto node = graph_.get(type);
        const auto result = lattice_.meet(node.children.back(), bound.back());
        if (node.children.size() == bound.size() && result != graph_.bottom()) {
            std::vector<Id> inputs(node.children.begin(), node.children.end() - 1);
            types.push_back(lattice_.fun(std::move(inputs), result, std::ranges::contains(node.labels, "exact")));
        }
    }
    return types.empty() ? graph_.bottom() : lattice_.overloaded(std::move(types));
}

Id Meet::tuples(const Id left, const Id right) {
    if (any(left, "tuple") || any(right, "tuple")) {
        return any(left, "tuple") ? right : left;
    }
    const auto a = graph_.get(left).children;
    const auto b = graph_.get(right).children;
    if (a.size() != b.size()) {
        return graph_.bottom();
    }
    std::vector<Id> elements;
    elements.reserve(a.size());
    for (std::size_t index = 0; index < a.size(); ++index) {
        elements.push_back(lattice_.meet(a[index], b[index]));
    }
    return std::ranges::contains(elements, graph_.bottom()) ? graph_.bottom() : lattice_.tuple(std::move(elements));
}

Id Meet::maps(const Id left, const Id right) {
    if (any(left, "map") || any(right, "map")) {
        return any(left, "map") ? right : left;
    }
    if (graph_.get(left).name == "association" || graph_.get(right).name == "association") {
        return associated(left, right);
    }
    return exact_maps(left, right);
}

Id Meet::exact_maps(const Id left, const Id right) {
    if (left == right) {
        return left;
    }
    auto fields = graph_.get(left).children;
    const auto other = graph_.get(right).children;
    if (fields.size() != other.size()) {
        return graph_.bottom();
    }
    // Exact maps hold exactly their keys: other keys share no value.
    for (std::size_t index = 0; index < fields.size(); index += 2) {
        if (fields[index] != other[index]) {
            return graph_.bottom();
        }
        fields[index + 1] = lattice_.meet(fields[index + 1], other[index + 1]);
    }
    return std::ranges::contains(fields, graph_.bottom()) ? graph_.bottom() : lattice_.map(std::move(fields));
}

Id Meet::associated(const Id left, const Id right) {
    if (left == right) {
        return left;
    }
    const auto a = graph_.get(left);
    const auto b = graph_.get(right);
    if (a.name == "association" && b.name == "association") {
        return lattice_.association(lattice_.meet(a.children[0], b.children[0]),
                                    lattice_.meet(a.children[1], b.children[1]));
    }
    // An exact map within an association: every key among its keys, every value among its values.
    const auto &exact = a.name == "association" ? b : a;
    const auto &association = a.name == "association" ? a : b;
    auto fields = exact.children;
    for (std::size_t index = 0; index + 1 < fields.size(); index += 2) {
        fields[index + 1] = lattice_.meet(fields[index + 1], association.children[1]);
        if (lattice_.meet(fields[index], association.children[0]) == graph_.bottom()) {
            return graph_.bottom();
        }
    }
    return std::ranges::contains(fields, graph_.bottom()) ? graph_.bottom() : lattice_.map(std::move(fields));
}

ListShape Meet::list_shape(const Id fact) {
    const auto node = graph_.get(fact);
    const auto top = graph_.top();
    if (node.kind == Kind::list) {
        return node.children.empty()
                   ? ListShape{ListShape::Kind::nil, top, top}
                   : ListShape{ListShape::Kind::proper, node.children[0], top, node.name == "nonempty"};
    }
    if (node.name == "nonempty_improper_list" && node.children.size() == 2) {
        return {ListShape::Kind::improper, node.children[0], node.children[1]};
    }
    if (node.name == "string" || node.name == "nonempty_string") {
        return {ListShape::Kind::proper, lattice_.range({"0", CHAR_LIMIT}), top, node.name == "nonempty_string"};
    }
    if (node.name == "nonempty_maybe_improper_list") {
        return {ListShape::Kind::nonempty, top, top, true};
    }
    return {node.name == "list" ? ListShape::Kind::proper : ListShape::Kind::any, top, top};
}

Id Meet::lists(const Id left, const Id right) {
    if (graph_.get(left).kind == Kind::positional || graph_.get(right).kind == Kind::positional) {
        return graph_.get(left).kind == Kind::positional ? positional(left, right) : positional(right, left);
    }
    const auto a = list_shape(left);
    const auto b = list_shape(right);
    if (const auto result = loose(a, b, {left, right})) {
        return *result;
    }
    if ((a.kind == ListShape::Kind::improper) != (b.kind == ListShape::Kind::improper)) {
        return graph_.bottom();
    }
    return proper(a, b);
}

Id Meet::proper(const ListShape &left, const ListShape &right) {
    if (left.kind == ListShape::Kind::improper) {
        return lattice_.improper(lattice_.meet(left.head, right.head), lattice_.meet(left.tail, right.tail));
    }
    if (left.kind == ListShape::Kind::nil || right.kind == ListShape::Kind::nil) {
        const auto &other = left.kind == ListShape::Kind::nil ? right : left;
        return other.nonempty ? graph_.bottom() : lattice_.nil();
    }
    return lattice_.list(lattice_.meet(left.head, right.head), left.nonempty || right.nonempty);
}

std::optional<Id> Meet::loose(const ListShape &a, const ListShape &b, const std::pair<Id, Id> &facts) {
    if (a.kind == ListShape::Kind::any || b.kind == ListShape::Kind::any) {
        return a.kind == ListShape::Kind::any ? facts.second : facts.first;
    }
    if (a.kind != ListShape::Kind::nonempty && b.kind != ListShape::Kind::nonempty) {
        return std::nullopt;
    }
    return a.kind == ListShape::Kind::nonempty ? nonempty(b, facts.second) : nonempty(a, facts.first);
}

Id Meet::positional(const Id fact, const Id other) {
    auto children = graph_.get(fact).children;
    const auto node = graph_.get(other);
    const auto shape = node.kind == Kind::positional ? ListShape{ListShape::Kind::any, fact, fact} : list_shape(other);
    if (node.kind == Kind::positional && node.children.size() == children.size()) {
        for (std::size_t index = 0; index < children.size(); ++index) {
            children[index] = lattice_.meet(children[index], node.children[index]);
        }
    } else if (shape.kind == ListShape::Kind::nil) {
        return graph_.bottom();
    } else if (shape.kind == ListShape::Kind::proper) {
        // Every element is one of the list's; the tail is the rest of such a list.
        for (std::size_t index = 0; index + 1 < children.size(); ++index) {
            children[index] = lattice_.meet(children[index], shape.head);
        }
        children.back() = lattice_.meet(children.back(), lattice_.list(shape.head, false));
    }
    // Positional lists of other lengths, improper lists and any list keep the positional side: it holds the values
    // both share.
    return std::ranges::contains(children, graph_.bottom()) ? graph_.bottom()
                                                            : lattice_.positional(std::move(children));
}

Id Meet::nonempty(const ListShape &other, const Id fact) {
    switch (other.kind) {
    case ListShape::Kind::nil:
        return graph_.bottom();
    case ListShape::Kind::proper:
        return lattice_.list(other.head, true);
    default:
        return fact;
    }
}

Sizes Meet::sizes(const Id fact) const {
    const auto &node = graph_.get(fact);
    static const std::map<std::string_view, Sizes> NAMED{
        {"binary", {0, 8}}, {"nonempty_binary", {8, 8}}, {"bitstring", {0, 1}}, {"nonempty_bitstring", {1, 1}}};
    if (const auto found = NAMED.find(node.name); node.kind == Kind::application && found != NAMED.end()) {
        return found->second;
    }
    Sizes result;
    for (std::size_t index = 0; index < node.children.size() && index < node.labels.size(); ++index) {
        std::uint64_t value = 0;
        const auto &digits = graph_.get(node.children[index]).name;
        std::from_chars(digits.data(), digits.data() + digits.size(), value);
        (node.labels[index] == "unit" ? result.unit : result.base) = value;
    }
    return result;
}

Id Meet::bitstrings(const Id left, const Id right) {
    // The narrower of two nested size sets; overlapping ones keep the left side, which holds the shared sizes.
    return sizes_within(sizes(right), sizes(left)) ? right : left;
}

bool Meet::elements_within(const Id inner, const Id outer) {
    if (inner == outer) {
        return true;
    }
    // Copies: `within` may grow the graph.
    const auto a = graph_.get(inner).children;
    const auto b = graph_.get(outer).children;
    if (any(inner, "tuple") || a.size() != b.size()) {
        return false;
    }
    for (std::size_t index = 0; index < a.size(); ++index) {
        if (!contained(a[index], b[index])) {
            return false;
        }
    }
    return true;
}

bool Meet::contained(const Id inner, const Id outer) {
    if (inner == outer) {
        return true;
    }
    const auto outers = lattice_.members(outer);
    return std::ranges::all_of(lattice_.members(inner), [&](const Id member) {
        return std::ranges::any_of(outers, [&](const Id candidate) { return within(member, candidate); });
    });
}

bool Meet::within(const Id inner, const Id outer) {
    if (inner == outer || outer == graph_.top()) {
        return true;
    }
    if (inner == graph_.top() || family(inner) != family(outer) || family(inner) == OTHER) {
        return false;
    }
    using Test = bool (Meet::*)(Id, Id);
    static const std::map<int, Test> TESTS{{NUMBERS, &Meet::same_meet},     {ATOMS, &Meet::same_atoms},
                                           {FUNS, &Meet::any_fun},          {TUPLES, &Meet::any_tuple},
                                           {MAPS, &Meet::any_map},          {LISTS, &Meet::any_list},
                                           {BITSTRINGS, &Meet::fewer_sizes}};
    const auto test = TESTS.find(family(inner));
    return test == TESTS.end() || (this->*test->second)(inner, outer);
}
} // namespace

Id Lattice::meet(Id left, Id right) {
    if (left == graph_.top() || right == graph_.top()) {
        return left == graph_.top() ? right : left;
    }
    Meet meet(*this);
    std::vector<Id> results;
    for (const auto a : members(left)) {
        for (const auto b : members(right)) {
            results.push_back(meet.members(a, b));
        }
    }
    return join(results, 0);
}

bool Lattice::within(Id inner, Id outer) {
    Meet meet(*this);
    return meet.contained(inner, outer);
}

Id Lattice::subtract(Id fact, Id removed) {
    if (fact == removed) {
        return graph_.bottom();
    }
    Meet meet(*this);
    std::vector<Id> results;
    for (const auto member : members(fact)) {
        if (family(member) == NUMBERS && family(removed) == NUMBERS) {
            results.push_back(subtract_numbers(member, removed));
        } else if (!meet.within(member, removed)) {
            results.push_back(member);
        }
    }
    return join(results, 0);
}

Id Lattice::subtract_numbers(Id member, Id removed) {
    if (member == removed) {
        return graph_.bottom();
    }
    // A number category loses the integers or floats `removed` holds whole.
    const auto kept = numbers(member);
    const auto gone = numbers(removed);
    const bool all_integers = gone.integers && !gone.low && !gone.high;
    std::vector<Id> results;
    if (kept.integers && !all_integers) {
        results.push_back(interval(kept.low, kept.high));
    }
    if (kept.floats && !gone.floats) {
        results.push_back(category("float"));
    }
    return join(results, 0);
}
} // namespace clause::semantic::types
