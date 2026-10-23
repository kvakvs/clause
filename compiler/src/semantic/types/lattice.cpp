#include "lattice.hpp"
#include "decimal.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>

// Joins and widening of facts (docs/semantic.md#inference-domain): the members of the joined facts are sorted into
// families (atoms, numbers, identifiers, funs, tuples, maps, lists, bitstrings), each family is rebuilt by its rule
// and the members are assembled in term order.
namespace clause::semantic::types {
namespace {
// The largest character code: a list of 0..CHAR_LIMIT is string().
constexpr std::string_view CHAR_LIMIT = "1114111";

// Orders canonical decimal integers by value.
struct DecimalOrder {
    bool operator()(const std::string &left, const std::string &right) const { return decimal_less(left, right); }
};

// An integer interval; a missing bound is unbounded.
struct Interval {
    std::optional<std::string> low;
    std::optional<std::string> high;
    bool operator==(const Interval &) const = default;
};

// Whether lower bound `left` is below `right`; a missing bound is below every other.
bool lower_below(const std::optional<std::string> &left, const std::optional<std::string> &right) {
    return right && (!left || decimal_less(*left, *right));
}

// Whether upper bound `left` is above `right`; a missing bound is above every other.
bool upper_above(const std::optional<std::string> &left, const std::optional<std::string> &right) {
    return right && (!left || decimal_less(*right, *left));
}

// A lower bound that moved down, moved on to the next threshold: 1 (pos_integer()), 0 (non_neg_integer()), else
// unbounded.
std::optional<std::string> lower_threshold(const std::optional<std::string> &low) {
    if (low && !decimal_less(*low, "1")) {
        return "1";
    }
    if (low && !decimal_less(*low, "0")) {
        return "0";
    }
    return std::nullopt;
}

// An upper bound that moved up, moved on to the next threshold: -1 (neg_integer()), else unbounded.
std::optional<std::string> upper_threshold(const std::optional<std::string> &high) {
    return high && decimal_less(*high, "0") ? std::optional<std::string>("-1") : std::nullopt;
}

// The smallest interval holding both.
Interval hull(const Interval &left, const Interval &right) {
    return {lower_below(left.low, right.low) ? left.low : right.low,
            upper_above(left.high, right.high) ? left.high : right.high};
}

// A proper list's element fact and whether it is never empty.
struct ListShape {
    Id element;
    bool nonempty = false;
};

// A bitstring of `base` bits plus any multiple of `unit` bits; unit 0 is exactly `base` bits.
struct Bits {
    std::uint64_t base = 0;
    std::uint64_t unit = 0;
};

// The smallest bitstring set holding both: the shorter base, and every difference of sizes as a unit.
Bits combine(Bits left, Bits right) {
    const auto base = std::min(left.base, right.base);
    const auto difference = std::max(left.base, right.base) - base;
    return {base, std::gcd(std::gcd(left.unit, right.unit), difference)};
}

// The members of facts grouped by family: singleton atoms and integers, an integer interval, the identifier
// categories (pid, port, reference), funs, tuples, maps, proper and improper lists, a bitstring set, and facts of no
// family (declared named types, records), kept as they are. The flags record categories: atom(), float(),
// number(), fun(), tuple(), map(), and the empty list.
struct Families {
    std::set<std::string> atoms;
    std::set<std::string, DecimalOrder> integers;
    std::set<std::string> identifiers;
    std::vector<Id> funs;
    std::vector<Id> tuples;
    std::vector<Id> maps;
    std::vector<ListShape> lists;
    std::vector<Id> improper;
    std::vector<Id> positional;
    std::vector<Id> others;
    std::optional<Interval> interval;
    std::optional<Bits> bits;
    bool any_atom = false;
    bool floats = false;
    bool numbers = false;
    bool any_fun = false;
    bool any_tuple = false;
    bool any_map = false;
    bool nil = false;
    // Whether a member is no number.
    bool nonnumeric = false;

    void add_interval(const Interval &added) { interval = interval ? hull(*interval, added) : added; }

    void add_bits(Bits added) { bits = bits ? combine(*bits, added) : added; }

    // The integers as one interval, if there are any.
    std::optional<Interval> bounds() const {
        auto result = interval;
        for (const auto &value : integers) {
            result = result ? hull(*result, {value, value}) : Interval{value, value};
        }
        return result;
    }
};

// A non-negative decimal as a bit count; a size too large for one saturates.
std::uint64_t bit_count(std::string_view decimal) {
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(decimal.data(), decimal.data() + decimal.size(), value);
    return parsed.ec == std::errc{} ? value : UINT64_MAX;
}

int family_rank(const Node &node);

// The facts char() and any improper list, interned by the first join and then read from the graph: the same Ids, in
// the same interning order. Added for the Gather constants (OPTC.A).
Graph::JoinConstants join_constants(Lattice &lattice) {
    auto &graph = lattice.graph();
    if (!graph.join_constants()) {
        // Characters first, as Gather interned them before.
        const auto characters = lattice.range({"0", CHAR_LIMIT});
        graph.keep_join_constants({characters, lattice.improper(graph.top(), graph.top())});
    }
    return *graph.join_constants();
}

// Sorts the members of facts into their families. Nothing is interned while it walks the graph.
class Gather final {
  public:
    // With `plain`, positional lists are added as their plain list facts.
    explicit Gather(Lattice &lattice, bool plain = false)
        : lattice_(lattice), graph_(lattice.graph()), constants_(join_constants(lattice)), plain_(plain) {}

    // Add every member of `fact`: a union's members, or the fact itself.
    void add(Id fact) {
        // Read in place: only plain() below interns, after which this node is not read again; a union's members are
        // read by index, as adding one may intern. Changed for the node copies in Gather (OPTC.A3).
        const auto &node = graph_.get(fact);
        if (node.kind == Kind::union_type) {
            for (std::size_t index = 0; index < graph_.get(fact).children.size(); ++index) {
                add(graph_.get(fact).children[index]);
            }
            return;
        }
        if (node.kind == Kind::positional && plain_) {
            add(lattice_.plain(fact));
            return;
        }
        families.nonnumeric = families.nonnumeric || (node.kind != Kind::bottom && family_rank(node) != 0);
        if (node.kind == Kind::positional) {
            families.positional.push_back(fact);
        } else if (!scalar(node) && !container(fact, node)) {
            families.others.push_back(fact);
        }
    }

    Families families;

  private:
    // Atoms, integers, ranges and categories; false for another kind.
    bool scalar(const Node &node) {
        switch (node.kind) {
        case Kind::bottom:
            return true;
        case Kind::atom:
            families.atoms.insert(node.name);
            return true;
        case Kind::integer:
            families.integers.insert(node.name);
            return true;
        case Kind::range:
            families.add_interval({graph_.get(node.children.at(0)).name, graph_.get(node.children.at(1)).name});
            return true;
        case Kind::application:
            return category(node);
        default:
            return false;
        }
    }

    // Tuples, lists, maps, funs and bitstrings; false for another kind.
    bool container(Id fact, const Node &node) {
        switch (node.kind) {
        case Kind::tuple:
            add_tuple(fact, node);
            return true;
        case Kind::list:
            add_list(node);
            return true;
        case Kind::map:
            add_map(fact, node);
            return true;
        case Kind::function:
            add_fun(fact, node);
            return true;
        case Kind::bitstring:
            families.add_bits(bits(node));
            return true;
        case Kind::application:
            return improper(fact, node);
        default:
            return false;
        }
    }

    // An improper list nonempty_improper_list(Head, Tail); false for another named type.
    bool improper(Id fact, const Node &node) {
        if (node.name != "nonempty_improper_list" || node.module != "erlang" || node.children.size() != 2) {
            return false;
        }
        families.improper.push_back(fact);
        return true;
    }

    // A tuple of known elements, or tuple().
    void add_tuple(Id fact, const Node &node) {
        if (node.name == "any") {
            families.any_tuple = true;
        } else {
            families.tuples.push_back(fact);
        }
    }

    // The empty list, or a proper list of an element.
    void add_list(const Node &node) {
        if (node.children.empty()) {
            families.nil = true;
        } else {
            families.lists.push_back({node.children.front(), node.name == "nonempty"});
        }
    }

    // A map of exact keys, or map().
    void add_map(Id fact, const Node &node) {
        if (node.name == "any") {
            families.any_map = true;
        } else {
            families.maps.push_back(fact);
        }
    }

    // A fun with a known result, or fun().
    void add_fun(Id fact, const Node &node) {
        if (std::ranges::contains(node.labels, std::string("result"))) {
            families.funs.push_back(fact);
        } else {
            families.any_fun = true;
        }
    }

    // The base and unit of a bitstring node.
    Bits bits(const Node &node) const {
        Bits result;
        for (std::size_t index = 0; index < node.children.size() && index < node.labels.size(); ++index) {
            const auto size = bit_count(graph_.get(node.children[index]).name);
            (node.labels[index] == "unit" ? result.unit : result.base) = size;
        }
        return result;
    }

    // What a category adds to the families, by its name.
    using Rule = void (*)(Gather &, const Node &);

    // A built-in category; false for any other named type.
    bool category(const Node &node) {
        static const std::map<std::string_view, Rule> RULES = rules();
        const auto found = RULES.find(node.name);
        if (node.module != "erlang" || found == RULES.end()) {
            return false;
        }
        found->second(*this, node);
        return true;
    }

    static std::map<std::string_view, Rule> rules();

    Lattice &lattice_;
    const Graph &graph_;
    // The fact char(): 0..CHAR_LIMIT, and any improper list.
    Graph::JoinConstants constants_;
    bool plain_;
};

std::map<std::string_view, Gather::Rule> Gather::rules() {
    return {
        {"atom", [](Gather &g, const Node &) { g.families.any_atom = true; }},
        {"boolean", [](Gather &g, const Node &) { g.families.atoms.insert({"false", "true"}); }},
        {"integer", [](Gather &g, const Node &) { g.families.add_interval({}); }},
        {"non_neg_integer", [](Gather &g, const Node &) { g.families.add_interval({"0", std::nullopt}); }},
        {"pos_integer", [](Gather &g, const Node &) { g.families.add_interval({"1", std::nullopt}); }},
        {"neg_integer", [](Gather &g, const Node &) { g.families.add_interval({std::nullopt, "-1"}); }},
        {"float", [](Gather &g, const Node &) { g.families.floats = true; }},
        {"number", [](Gather &g, const Node &) { g.families.numbers = true; }},
        {"pid", [](Gather &g, const Node &node) { g.families.identifiers.insert(node.name); }},
        {"port", [](Gather &g, const Node &node) { g.families.identifiers.insert(node.name); }},
        {"reference", [](Gather &g, const Node &node) { g.families.identifiers.insert(node.name); }},
        {"tuple", [](Gather &g, const Node &) { g.families.any_tuple = true; }},
        {"map", [](Gather &g, const Node &) { g.families.any_map = true; }},
        {"fun", [](Gather &g, const Node &) { g.families.any_fun = true; }},
        {"function", [](Gather &g, const Node &) { g.families.any_fun = true; }},
        {"list", [](Gather &g, const Node &) { g.families.lists.push_back({g.graph_.top(), false}); }},
        {"string", [](Gather &g, const Node &) { g.families.lists.push_back({g.constants_.characters, false}); }},
        {"nonempty_string",
         [](Gather &g, const Node &) { g.families.lists.push_back({g.constants_.characters, true}); }},
        {"maybe_improper_list",
         [](Gather &g, const Node &) {
             g.families.lists.push_back({g.graph_.top(), false});
             g.families.improper.push_back(g.constants_.any_improper);
         }},
        {"nonempty_maybe_improper_list",
         [](Gather &g, const Node &) {
             g.families.lists.push_back({g.graph_.top(), true});
             g.families.improper.push_back(g.constants_.any_improper);
         }},
        {"binary", [](Gather &g, const Node &) { g.families.add_bits({0, 8}); }},
        {"nonempty_binary", [](Gather &g, const Node &) { g.families.add_bits({8, 8}); }},
        {"bitstring", [](Gather &g, const Node &) { g.families.add_bits({0, 1}); }},
        {"nonempty_bitstring", [](Gather &g, const Node &) { g.families.add_bits({1, 1}); }},
    };
}

// The rank of a fact's family in Erlang term order: number < atom < reference < fun < port < pid < tuple < map
// < nil < list < bitstring, then facts of no family.
// The family rank of a node of a kind other than a named category, by kind (the empty list ranks 8). Changed to a
// table for faster family lookups (OPTC.C).
int kind_rank(const Node &node) {
    // In the order of Kind: top, bottom, atom, integer, variable, range, application, tuple, list, map, record,
    // bitstring, function, unary, binary, annotation, union_type, reference, positional.
    static constexpr std::array<int, 19> RANKS{11, 11, 1, 0, 11, 0, 11, 6, 9, 7, 11, 10, 3, 11, 11, 11, 11, 11, 9};
    const auto rank = RANKS.at(static_cast<std::size_t>(node.kind));
    return node.kind == Kind::list && node.children.empty() ? 8 : rank;
}

int family_rank(const Node &node) {
    // Hashed, as every meet and join asks (changed for faster family lookups, OPTC.C).
    static const std::unordered_map<std::string_view, int> CATEGORIES{{"integer", 0},
                                                                      {"non_neg_integer", 0},
                                                                      {"pos_integer", 0},
                                                                      {"neg_integer", 0},
                                                                      {"float", 0},
                                                                      {"number", 0},
                                                                      {"atom", 1},
                                                                      {"boolean", 1},
                                                                      {"reference", 2},
                                                                      {"fun", 3},
                                                                      {"port", 4},
                                                                      {"pid", 5},
                                                                      {"tuple", 6},
                                                                      {"map", 7},
                                                                      {"list", 9},
                                                                      {"string", 9},
                                                                      {"nonempty_string", 9},
                                                                      {"nonempty_improper_list", 9},
                                                                      {"maybe_improper_list", 9},
                                                                      {"nonempty_maybe_improper_list", 9},
                                                                      {"binary", 10},
                                                                      {"bitstring", 10},
                                                                      {"nonempty_binary", 10},
                                                                      {"nonempty_bitstring", 10}};
    if (node.kind == Kind::application) {
        const auto found = CATEGORIES.find(node.name);
        return found == CATEGORIES.end() ? 11 : found->second;
    }
    return kind_rank(node);
}

bool term_less(const Graph &graph, Id left, Id right);

// Tuples by size, then element by element.
bool tuple_less(const Graph &graph, const Node &a, const Node &b) {
    if (a.children.size() != b.children.size()) {
        return a.children.size() < b.children.size();
    }
    const auto order = std::ranges::mismatch(a.children, b.children);
    return order.in1 != a.children.end() && term_less(graph, *order.in1, *order.in2);
}

// The order of two facts of one kind whose values have an order: integers by value, atoms by name, tuples; none for
// other facts.
std::optional<bool> value_less(const Graph &graph, const Node &a, const Node &b) {
    if (a.kind != b.kind) {
        return std::nullopt;
    }
    switch (a.kind) {
    case Kind::integer:
        return decimal_less(a.name, b.name);
    case Kind::atom:
        return a.name < b.name;
    case Kind::tuple:
        return tuple_less(graph, a, b);
    default:
        return std::nullopt;
    }
}

// Orders facts as their values are ordered (Erlang term order) where that is defined: by family, then by value;
// other facts by kind, children and graph identity, so the order is total.
bool term_less(const Graph &graph, Id left, Id right) {
    const auto &a = graph.get(left);
    const auto &b = graph.get(right);
    if (family_rank(a) != family_rank(b)) {
        return family_rank(a) < family_rank(b);
    }
    if (const auto order = value_less(graph, a, b)) {
        return *order;
    }
    return std::tie(a.kind, a.children, left) < std::tie(b.kind, b.children, right);
}

// The tag of a tuple: its first element's name when that is an atom, else empty. Read before the graph grows.
std::string_view tuple_tag(const Graph &graph, Id tuple) {
    const auto &node = graph.get(tuple);
    const auto *first = node.children.empty() ? nullptr : &graph.get(node.children.front());
    return first && first->kind == Kind::atom ? std::string_view(first->name) : std::string_view();
}

// The fact of an integer interval: a singleton, a range, or the category of an unbounded one.
Id interval_fact(Lattice &lattice, const Interval &bounds) {
    if (bounds.low && bounds.high) {
        return *bounds.low == *bounds.high ? lattice.integer(*bounds.low) : lattice.range({*bounds.low, *bounds.high});
    }
    if (bounds.low && !decimal_less(*bounds.low, "0")) {
        return lattice.category(decimal_less(*bounds.low, "1") ? "non_neg_integer" : "pos_integer");
    }
    if (bounds.high && decimal_less(*bounds.high, "0")) {
        return lattice.category("neg_integer");
    }
    return lattice.category("integer");
}

// Rebuilds each family of joined facts by its rule; members come out in term order.
class Assemble final {
  public:
    Assemble(Lattice &lattice, std::size_t depth) noexcept
        : lattice_(lattice), graph_(lattice.graph()), limits_(lattice.limits()), depth_(depth) {}

    // The members of the joined fact.
    std::vector<Id> members(const Families &families) {
        std::vector<Id> out;
        numbers(families, out);
        atoms(families, out);
        identifier(families, "reference", out);
        funs(families, out);
        identifier(families, "port", out);
        identifier(families, "pid", out);
        tuples(families, out);
        maps(families, out);
        lists(families, out);
        positional(families, out);
        improper(families, out);
        maybe_improper(out);
        if (families.bits) {
            out.push_back(lattice_.bitstring(families.bits->base, families.bits->unit));
        }
        auto others = families.others;
        std::ranges::sort(others);
        others.erase(std::unique(others.begin(), others.end()), others.end());
        out.insert(out.end(), others.begin(), others.end());
        return out;
    }

  private:
    // Integers and floats; with an interval of integers as well as floats, number().
    void numbers(const Families &families, std::vector<Id> &out) {
        const bool interval = families.interval.has_value() || families.integers.size() > limits_.singletons;
        if (families.numbers || (families.floats && interval)) {
            out.push_back(lattice_.category("number"));
            return;
        }
        if (const auto bounds = families.bounds(); interval && bounds) {
            out.push_back(interval_fact(lattice_, *bounds));
        } else {
            for (const auto &value : families.integers) {
                out.push_back(lattice_.integer(value));
            }
        }
        if (families.floats) {
            out.push_back(lattice_.category("float"));
        }
    }

    // Singleton atoms up to the budget, true and false as boolean(), else atom().
    void atoms(const Families &families, std::vector<Id> &out) {
        if (families.any_atom || families.atoms.size() > limits_.singletons) {
            out.push_back(lattice_.category("atom"));
        } else if (families.atoms == std::set<std::string>{"false", "true"}) {
            out.push_back(lattice_.category("boolean"));
        } else {
            for (const auto &name : families.atoms) {
                out.push_back(lattice_.atom(name));
            }
        }
    }

    void identifier(const Families &families, const std::string &name, std::vector<Id> &out) {
        if (families.identifiers.contains(name)) {
            out.push_back(lattice_.category(name));
        }
    }

    // Equal funs keep their function types and inputs; other funs of one arity join their results, funs of several
    // arities are fun().
    void funs(const Families &families, std::vector<Id> &out) {
        if (families.funs.empty() && !families.any_fun) {
            return;
        }
        if (families.any_fun) {
            out.push_back(lattice_.category("fun"));
            return;
        }
        const auto first = families.funs.front();
        if (std::ranges::all_of(families.funs, [&](Id fun) { return fun == first; })) {
            out.push_back(first);
            return;
        }
        out.push_back(joined_funs(families.funs));
    }

    // Different funs as one fun of any inputs: of their arity returning their joined results, or fun().
    Id joined_funs(const std::vector<Id> &funs) {
        std::vector<Id> plain;
        plain.reserve(funs.size());
        for (const auto fun : funs) {
            plain.push_back(lattice_.joined_fun(fun));
        }
        const auto arity = graph_.get(plain.front()).children.size();
        if (!std::ranges::all_of(plain, [&](Id fun) { return graph_.get(fun).children.size() == arity; })) {
            return lattice_.category("fun");
        }
        std::vector<Id> results;
        results.reserve(plain.size());
        for (const auto fun : plain) {
            results.push_back(graph_.get(fun).children.back());
        }
        return lattice_.fun(arity - 1, lattice_.join(results, depth_ + 1));
    }

    // Tuples of one size and tag (their first element when it is an atom) join element by element; more shapes than
    // the member budget are tuple().
    void tuples(const Families &families, std::vector<Id> &out) {
        std::vector<Id> merged;
        for (const auto tuple : families.tuples) {
            merge_tuple(merged, tuple);
        }
        if (families.any_tuple || merged.size() > limits_.members) {
            out.push_back(lattice_.category("tuple"));
            return;
        }
        std::ranges::sort(merged, [&](Id left, Id right) { return term_less(graph_, left, right); });
        out.insert(out.end(), merged.begin(), merged.end());
    }

    // Join `tuple` into the first shape of its size and tag, or add it as a shape.
    void merge_tuple(std::vector<Id> &merged, Id tuple) {
        const auto size = graph_.get(tuple).children.size();
        for (auto &shape : merged) {
            if (graph_.get(shape).children.size() == size && tuple_tag(graph_, shape) == tuple_tag(graph_, tuple)) {
                const auto left = graph_.get(shape).children;
                const auto right = graph_.get(tuple).children;
                std::vector<Id> elements;
                elements.reserve(size);
                for (std::size_t index = 0; index < size; ++index) {
                    elements.push_back(lattice_.join(std::array{left[index], right[index]}, depth_ + 1));
                }
                shape = lattice_.tuple(std::move(elements));
                return;
            }
        }
        merged.push_back(tuple);
    }

    // Maps with the same keys join value by value; maps with other keys are map().
    void maps(const Families &families, std::vector<Id> &out) {
        if (!families.any_map && families.maps.empty()) {
            return;
        }
        if (families.any_map) {
            out.push_back(lattice_.category("map"));
            return;
        }
        const auto keys = keys_of(families.maps.front());
        const bool same = std::ranges::all_of(
            families.maps, [&](Id map) { return graph_.get(map).name == "exact" && keys_of(map) == keys; });
        if (!same) {
            out.push_back(associations(families));
            return;
        }
        std::vector<Id> fields;
        fields.reserve(2 * keys.size());
        for (std::size_t index = 0; index < keys.size(); ++index) {
            std::vector<Id> values;
            values.reserve(families.maps.size());
            for (const auto map : families.maps) {
                values.push_back(graph_.get(map).children.at(2 * index + 1));
            }
            fields.push_back(keys[index]);
            fields.push_back(lattice_.join(values, depth_ + 1));
        }
        out.push_back(lattice_.map(std::move(fields)));
    }

    // Maps of different keys as one association of their joined keys and values.
    Id associations(const Families &families) {
        std::vector<Id> keys;
        std::vector<Id> values;
        for (const auto map : families.maps) {
            const auto children = graph_.get(map).children;
            for (std::size_t index = 0; index + 1 < children.size(); index += 2) {
                keys.push_back(children[index]);
                values.push_back(children[index + 1]);
            }
        }
        return lattice_.association(lattice_.join(keys, depth_ + 1), lattice_.join(values, depth_ + 1));
    }

    // The keys of an exact map fact, in their order.
    std::vector<Id> keys_of(Id map) const {
        const auto &children = graph_.get(map).children;
        std::vector<Id> keys;
        for (std::size_t index = 0; index < children.size(); index += 2) {
            keys.push_back(children[index]);
        }
        return keys;
    }

    // Proper lists join their elements; possibly empty when one is or [] joins them.
    void lists(const Families &families, std::vector<Id> &out) {
        if (families.lists.empty()) {
            if (families.nil) {
                out.push_back(lattice_.nil());
            }
            return;
        }
        std::vector<Id> elements;
        bool nonempty = !families.nil;
        for (const auto &shape : families.lists) {
            elements.push_back(shape.element);
            nonempty = nonempty && shape.nonempty;
        }
        out.push_back(lattice_.list(lattice_.join(elements, depth_ + 1), nonempty));
    }

    // Positional lists of one length (the only list facts present) join position by position, tails too.
    void positional(const Families &families, std::vector<Id> &out) {
        if (families.positional.empty()) {
            return;
        }
        const auto size = graph_.get(families.positional.front()).children.size();
        std::vector<Id> children;
        children.reserve(size);
        for (std::size_t index = 0; index < size; ++index) {
            std::vector<Id> facts;
            facts.reserve(families.positional.size());
            for (const auto list : families.positional) {
                facts.push_back(graph_.get(list).children[index]);
            }
            children.push_back(lattice_.join(facts, depth_ + 1));
        }
        out.push_back(lattice_.positional(std::move(children)));
    }

    // Any list and any improper list together are maybe_improper_list(), or nonempty_maybe_improper_list() without
    // the empty list.
    void maybe_improper(std::vector<Id> &out) {
        const auto improper = std::ranges::find(out, lattice_.improper(graph_.top(), graph_.top()));
        if (improper == out.end()) {
            return;
        }
        for (const auto &[list, merged] : {std::pair{lattice_.category("list"), "maybe_improper_list"},
                                           {lattice_.list(graph_.top(), true), "nonempty_maybe_improper_list"}}) {
            if (std::ranges::contains(out, list)) {
                out.erase(std::ranges::find(out, lattice_.improper(graph_.top(), graph_.top())));
                *std::ranges::find(out, list) = lattice_.category(merged);
                return;
            }
        }
    }

    // Improper lists join their elements and their tails.
    void improper(const Families &families, std::vector<Id> &out) {
        if (families.improper.empty()) {
            return;
        }
        std::vector<Id> heads;
        std::vector<Id> tails;
        heads.reserve(families.improper.size());
        tails.reserve(families.improper.size());
        for (const auto list : families.improper) {
            heads.push_back(graph_.get(list).children.at(0));
            tails.push_back(graph_.get(list).children.at(1));
        }
        const auto head = lattice_.join(heads, depth_ + 1);
        const auto tail = lattice_.join(tails, depth_ + 1);
        out.push_back(lattice_.improper(head, tail));
    }

    Lattice &lattice_;
    Graph &graph_;
    const FactLimits &limits_;
    // Containers around the facts being joined.
    std::size_t depth_;
};

// Whether a union member is an exact tuple: the one kind a union keeps several shapes of (one per size and tag), so
// one shape can hold another. Other families join into one member each.
bool exact_tuple(const Graph &graph, const Id member) {
    const auto &node = graph.get(member);
    return node.kind == Kind::tuple && node.name == "exact";
}

// Whether tuple member `index` adds nothing: another tuple of its size holds it, and when both hold each other (the
// same values in another shape) the earlier one stays. Added for step 58S.
bool subsumed(Lattice &lattice, const std::vector<Id> &members, const std::size_t index) {
    const auto &graph = lattice.graph();
    const auto member = members[index];
    if (!exact_tuple(graph, member)) {
        return false;
    }
    const auto size = graph.get(member).children.size();
    for (std::size_t other = 0; other < members.size(); ++other) {
        if (other == index || !exact_tuple(graph, members[other]) ||
            graph.get(members[other]).children.size() != size || !lattice.within(member, members[other])) {
            continue;
        }
        if (other < index || !lattice.within(members[other], member)) {
            return true;
        }
    }
    return false;
}

// A union's members without those another member holds, so one set of values has one union and rounds that reach
// the same values stop changing (step 58S).
std::vector<Id> without_subsumed(Lattice &lattice, const std::vector<Id> &members) {
    std::vector<Id> kept;
    for (std::size_t index = 0; index < members.size(); ++index) {
        if (!subsumed(lattice, members, index)) {
            kept.push_back(members[index]);
        }
    }
    return kept;
}

// The fact whose members are `families`, joined `depth` containers deep.
Id fact_of(Lattice &lattice, const Families &families, std::size_t depth) {
    auto &graph = lattice.graph();
    auto members = without_subsumed(lattice, Assemble(lattice, depth).members(families));
    if (members.empty()) {
        return graph.bottom();
    }
    if (members.size() == 1) {
        return members.front();
    }
    if (members.size() > lattice.limits().members) {
        return graph.top();
    }
    return graph.intern({Kind::union_type, {}, {}, std::move(members)});
}

// Whether positional lists, if any, can join position by position: they are the only list facts and of one length.
bool positional_fits(const Graph &graph, const Families &families) {
    if (families.positional.empty()) {
        return true;
    }
    const auto size = graph.get(families.positional.front()).children.size();
    return families.lists.empty() && !families.nil && families.improper.empty() &&
           std::ranges::all_of(families.positional, [&](Id list) { return graph.get(list).children.size() == size; });
}

// Whether a fact node is an overloaded fun, whose children are funs at its own level.
bool overloaded_fun(const Node &node) { return node.kind == Kind::function && node.name == "clauses"; }

// Whether a fact node is a container whose children are facts nested one level deeper.
bool is_container(const Node &node) {
    constexpr std::array containers{Kind::tuple,    Kind::list,        Kind::map,
                                    Kind::function, Kind::application, Kind::positional};
    return std::ranges::contains(containers, node.kind) && !node.children.empty() && !overloaded_fun(node);
}

// How many containers deep a fact nests, counted no further than `limit`. Each answer is kept in the graph (changed
// for memoized nesting, OPTC.C).
std::size_t nesting(const Graph &graph, Id fact, std::size_t limit) {
    if (const auto known = graph.nesting(fact, limit)) {
        return *known;
    }
    const auto &node = graph.get(fact);
    const bool container = is_container(node);
    if (limit == 0 || (!container && node.kind != Kind::union_type && !overloaded_fun(node))) {
        return 0;
    }
    std::size_t inner = 0;
    // Read in place: nothing interns while nesting walks.
    for (const auto child : node.children) {
        inner = std::max(inner, nesting(graph, child, limit - 1));
    }
    const auto depth = (container ? 1 : 0) + inner;
    graph.keep_nesting(fact, limit, depth);
    return depth;
}

Id relax(Lattice &lattice, Id previous, Id joined);

// Moves the integers of `after` on to the next threshold where a bound moved past `before`'s; whether one did.
bool relax_bounds(const Families &before, Families &after) {
    const auto old_bounds = before.bounds();
    const auto new_bounds = after.bounds();
    if (!old_bounds || !new_bounds) {
        return false;
    }
    auto relaxed = *new_bounds;
    if (lower_below(new_bounds->low, old_bounds->low)) {
        relaxed.low = lower_threshold(new_bounds->low);
    }
    if (upper_above(new_bounds->high, old_bounds->high)) {
        relaxed.high = upper_threshold(new_bounds->high);
    }
    if (relaxed == *new_bounds) {
        return false;
    }
    after.integers.clear();
    after.interval = relaxed;
    return true;
}

// Whether two container facts have one shape whose children correspond by position: the same kind, name and size,
// and for tuples the same tag, for exact maps the same keys. Added for nested integer widening (CONV.0).
bool same_shape(const Graph &graph, Id left, Id right) {
    const auto &a = graph.get(left);
    const auto &b = graph.get(right);
    if (a.kind != b.kind || a.name != b.name || a.labels != b.labels || a.children.size() != b.children.size()) {
        return false;
    }
    if (a.kind == Kind::tuple) {
        return tuple_tag(graph, left) == tuple_tag(graph, right);
    }
    if (a.kind == Kind::map && a.name == "exact") {
        for (std::size_t index = 0; index < a.children.size(); index += 2) {
            if (a.children[index] != b.children[index]) {
                return false;
            }
        }
    }
    return true;
}

// Relaxes each container of `after` against the container of its shape in `before`, child by child; whether one
// changed. Added for nested integer widening (CONV.0).
bool relax_shapes(Lattice &lattice, const std::vector<Id> &before, std::vector<Id> &after) {
    auto &graph = lattice.graph();
    bool changed = false;
    for (auto &fact : after) {
        const auto old = std::ranges::find_if(before, [&](Id candidate) { return same_shape(graph, candidate, fact); });
        if (old == before.end() || *old == fact) {
            continue;
        }
        const auto old_children = graph.get(*old).children;
        auto node = graph.get(fact);
        bool relaxed = false;
        for (std::size_t index = 0; index < node.children.size(); ++index) {
            const auto child = relax(lattice, old_children[index], node.children[index]);
            relaxed = relaxed || child != node.children[index];
            node.children[index] = child;
        }
        if (relaxed) {
            // Relaxed integers nest no deeper, so the node keeps within the budgets as it is.
            fact = graph.intern(std::move(node));
            changed = true;
        }
    }
    return changed;
}

// Relaxes the element of the proper list of `after` against `before`'s, when each has one; whether it changed.
// Added for nested integer widening (CONV.0).
bool relax_lists(Lattice &lattice, const std::vector<ListShape> &before, std::vector<ListShape> &after) {
    if (before.size() != 1 || after.size() != 1) {
        return false;
    }
    const auto element = relax(lattice, before.front().element, after.front().element);
    const bool changed = element != after.front().element;
    after.front().element = element;
    return changed;
}

// `joined` with every integer bound that moved past `previous`'s on to its next threshold, at the top and inside
// containers of one shape at every level, so a counter inside a record stops creeping (CONV.0).
Id relax(Lattice &lattice, Id previous, Id joined) {
    const auto &graph = lattice.graph();
    if (previous == graph.bottom() || joined == previous || joined == graph.top()) {
        return joined;
    }
    Gather before(lattice);
    before.add(previous);
    Gather after(lattice);
    after.add(joined);
    const auto &old = before.families;
    auto &families = after.families;
    bool changed = relax_bounds(old, families);
    changed = relax_lists(lattice, old.lists, families.lists) || changed;
    for (const auto &[old_facts, facts] : {std::pair{&old.tuples, &families.tuples},
                                           {&old.maps, &families.maps},
                                           {&old.improper, &families.improper},
                                           {&old.positional, &families.positional}}) {
        changed = relax_shapes(lattice, *old_facts, *facts) || changed;
    }
    return changed ? fact_of(lattice, families, 0) : joined;
}
} // namespace

Id Lattice::join(Id left, Id right) { return join(std::array{left, right}, 0); }

Id Lattice::join(std::span<const Id> members, std::size_t depth) {
    if (depth > limits_.depth) {
        return graph_.top();
    }
    Gather gather(*this);
    for (const auto member : members) {
        if (member == graph_.top()) {
            return graph_.top();
        }
        gather.add(member);
    }
    if (positional_fits(graph_, gather.families)) {
        return fact_of(*this, gather.families, depth);
    }
    // Positional lists that join with other list facts, or of other lengths, become plain lists.
    Gather plain(*this, true);
    for (const auto member : members) {
        plain.add(member);
    }
    return fact_of(*this, plain.families, depth);
}

// Relaxes nested integer bounds too (changed for nested integer widening, CONV.0).
Id Lattice::widen(Id previous, Id next) { return relax(*this, previous, join(previous, next)); }

int Lattice::family(Id fact) const { return family_rank(graph_.get(fact)); }

Id Lattice::interval(const std::optional<std::string> &low, const std::optional<std::string> &high) {
    return interval_fact(*this, {low, high});
}

std::vector<Id> Lattice::members(Id fact) const {
    const auto &node = graph_.get(fact);
    return node.kind == Kind::union_type ? node.children : std::vector<Id>{fact};
}

Numbers Lattice::numbers(Id fact) {
    if (fact == graph_.top()) {
        return {true, std::nullopt, std::nullopt, true, true};
    }
    Gather gather(*this);
    gather.add(fact);
    const auto &families = gather.families;
    Numbers result;
    if (const auto bounds = families.bounds(); bounds || families.numbers) {
        result.integers = true;
        result.low = families.numbers ? std::nullopt : bounds->low;
        result.high = families.numbers ? std::nullopt : bounds->high;
    }
    result.floats = families.floats || families.numbers;
    result.others = families.nonnumeric;
    return result;
}

bool Lattice::holds_atom(Id fact, std::string_view name) {
    if (fact == graph_.top()) {
        return true;
    }
    Gather gather(*this);
    gather.add(fact);
    return gather.families.any_atom || gather.families.atoms.contains(std::string(name));
}

Id Lattice::integer(std::string_view decimal) { return graph_.intern({Kind::integer, std::string(decimal)}); }

Id Lattice::atom(std::string_view name) { return graph_.intern({Kind::atom, std::string(name)}); }

Id Lattice::category(std::string_view name) {
    // fun() is a type of its own syntax, as a declaration makes it; its name is a reserved word.
    if (name == "fun") {
        return graph_.intern({Kind::function, "any_arguments"});
    }
    return graph_.intern({Kind::application, std::string(name), "erlang"});
}

Id Lattice::range(IntegerBounds bounds) {
    const auto first = integer(bounds.low);
    const auto last = integer(bounds.high);
    return graph_.intern({Kind::range, {}, {}, {first, last}});
}

Id Lattice::within_depth(Id fact) { return limits_.depth == 0 ? graph_.top() : truncate(fact, limits_.depth - 1); }

Id Lattice::truncate(Id fact, std::size_t levels) {
    if (nesting(graph_, fact, levels + 1) <= levels) {
        return fact;
    }
    auto node = graph_.get(fact);
    const bool container = is_container(node);
    if (container && levels == 0) {
        return graph_.top();
    }
    for (auto &child : node.children) {
        child = truncate(child, container ? levels - 1 : levels);
    }
    // A union's truncated members may now overlap: join them again.
    return node.kind == Kind::union_type ? join(node.children, 0) : graph_.intern(std::move(node));
}

Id Lattice::tuple(std::vector<Id> elements) {
    for (auto &element : elements) {
        element = within_depth(element);
    }
    // Past the element budget a tuple keeps its elements only when every one is known.
    if (elements.size() > limits_.elements && std::ranges::contains(elements, graph_.top())) {
        return category("tuple");
    }
    return graph_.intern({Kind::tuple, "exact", {}, std::move(elements)});
}

Id Lattice::list(Id element, bool nonempty) {
    element = within_depth(element);
    if (element == graph_.bottom()) {
        return nonempty ? graph_.bottom() : nil();
    }
    // The kept char() once a join interned it (changed for fewer interns, OPTC.C).
    const auto &constants = graph_.join_constants();
    if (element == (constants ? constants->characters : range({"0", CHAR_LIMIT}))) {
        return category(nonempty ? "nonempty_string" : "string");
    }
    if (element == graph_.top() && !nonempty) {
        return category("list");
    }
    return graph_.intern({Kind::list, nonempty ? "nonempty" : "possibly_empty", {}, {element}});
}

Id Lattice::improper(Id head, Id tail) {
    if (head == graph_.bottom() || tail == graph_.bottom()) {
        return graph_.bottom();
    }
    return graph_.intern(
        {Kind::application, "nonempty_improper_list", "erlang", {within_depth(head), within_depth(tail)}});
}

Id Lattice::positional(std::vector<Id> children) {
    for (auto &child : children) {
        child = within_depth(child);
    }
    return graph_.intern({Kind::positional, {}, {}, std::move(children)});
}

Id Lattice::plain(Id fact) {
    const auto children = graph_.get(fact).children;
    const std::span elements(children.begin(), children.end() - 1);
    return prepend(join(elements, 0), children.back());
}

Id Lattice::prepend(Id element, Id tail) {
    const auto node = graph_.get(tail);
    if (tail == graph_.top() || tail == graph_.bottom()) {
        // A cell in front of any tail is never empty, though it may end improperly.
        return tail == graph_.bottom() ? tail : category("nonempty_maybe_improper_list");
    }
    if (node.kind == Kind::union_type) {
        return prepend_members(element, node.children);
    }
    if (node.kind == Kind::positional) {
        return prepend(element, plain(tail));
    }
    if (node.kind == Kind::list) {
        return list(node.children.empty() ? element : join(element, node.children.front()), true);
    }
    return node.kind == Kind::application ? prepend_named(element, node, tail) : improper(element, tail);
}

Id Lattice::prepend_members(Id element, std::span<const Id> members) {
    std::vector<Id> cells;
    cells.reserve(members.size());
    for (const auto member : members) {
        cells.push_back(prepend(element, member));
    }
    return join(cells, 0);
}

Id Lattice::prepend_named(Id element, const Node &node, Id tail) {
    static const std::map<std::string_view, std::string_view> LISTS{
        {"list", ""}, {"string", "string"}, {"nonempty_string", "string"}};
    if (const auto found = LISTS.find(node.name); found != LISTS.end()) {
        return list(join(element, found->second.empty() ? graph_.top() : range({"0", CHAR_LIMIT})), true);
    }
    if (node.name == "nonempty_improper_list") {
        return improper(join(element, node.children.front()), node.children.back());
    }
    // Any other list category leaves the shape unknown but nonempty; a value that is no list ends an improper list.
    return node.name.find("list") != std::string::npos ? category("nonempty_maybe_improper_list")
                                                       : improper(element, tail);
}

Id Lattice::nil() { return graph_.intern({Kind::list, "possibly_empty"}); }

Id Lattice::map(std::vector<Id> fields) {
    if (fields.size() / 2 > limits_.elements) {
        // Past the key budget the keys join into one association.
        std::vector<Id> keys;
        std::vector<Id> values;
        for (std::size_t index = 0; index + 1 < fields.size(); index += 2) {
            keys.push_back(fields[index]);
            values.push_back(fields[index + 1]);
        }
        return association(join(keys, 0), join(values, 0));
    }
    std::vector<std::pair<Id, Id>> pairs;
    for (std::size_t index = 0; index + 1 < fields.size(); index += 2) {
        pairs.emplace_back(fields[index], within_depth(fields[index + 1]));
    }
    std::ranges::sort(pairs,
                      [&](const auto &left, const auto &right) { return term_less(graph_, left.first, right.first); });
    Node node{Kind::map, "exact"};
    for (const auto &[key, value] : pairs) {
        node.children.push_back(key);
        node.children.push_back(value);
        node.labels.emplace_back("1");
    }
    return graph_.intern(std::move(node));
}

Id Lattice::association(Id key, Id value) {
    if (key == graph_.bottom() || value == graph_.bottom()) {
        return map({});
    }
    return graph_.intern({Kind::map, "association", {}, {within_depth(key), within_depth(value)}, {"0"}});
}

Id Lattice::fun(std::size_t arity, Id result) {
    std::vector<Id> children(arity, graph_.top());
    children.push_back(within_depth(result));
    return graph_.intern({Kind::function, "product", {}, std::move(children), {"result"}});
}

Id Lattice::fun(std::vector<Id> inputs, Id result, bool exact) {
    for (auto &input : inputs) {
        input = within_depth(input);
    }
    inputs.push_back(within_depth(result));
    std::vector<std::string> labels{"result"};
    if (exact && std::ranges::any_of(inputs, [&](Id input) { return input != graph_.top(); })) {
        labels.emplace_back("exact");
    }
    return graph_.intern({Kind::function, "product", {}, std::move(inputs), std::move(labels)});
}

Id Lattice::overloaded(std::vector<Id> types) {
    if (types.size() == 1) {
        return types.front();
    }
    return graph_.intern({Kind::function, "clauses", {}, std::move(types), {"result"}});
}

Id Lattice::joined_fun(Id fact) {
    const auto &node = graph_.get(fact);
    if (node.kind != Kind::function || node.name != "clauses") {
        return fact;
    }
    const auto types = node.children;
    std::vector<Id> results;
    results.reserve(types.size());
    for (const auto type : types) {
        results.push_back(graph_.get(type).children.back());
    }
    return fun(graph_.get(types.front()).children.size() - 1, join(results, 0));
}

std::vector<Id> Lattice::function_types(Id fact) const {
    const auto &node = graph_.get(fact);
    return node.kind == Kind::function && node.name == "clauses" ? node.children : std::vector<Id>{fact};
}

Id Lattice::bitstring(std::uint64_t base, std::uint64_t unit) {
    static constexpr std::array<std::tuple<std::uint64_t, std::uint64_t, std::string_view>, 4> NAMED{
        {{0, 8, "binary"}, {8, 8, "nonempty_binary"}, {0, 1, "bitstring"}, {1, 1, "nonempty_bitstring"}}};
    for (const auto &[named_base, named_unit, name] : NAMED) {
        if (base == named_base && unit == named_unit) {
            return category(name);
        }
    }
    Node node{Kind::bitstring};
    if (base > 0) {
        node.children.push_back(integer(std::to_string(base)));
        node.labels.emplace_back("base");
    }
    if (unit > 0) {
        node.children.push_back(integer(std::to_string(unit)));
        node.labels.emplace_back("unit");
    }
    return graph_.intern(std::move(node));
}
} // namespace clause::semantic::types
