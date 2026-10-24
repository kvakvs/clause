#include "inference_narrowing.hpp"
#include "../../preprocessor/value.hpp"
#include "../capabilities.hpp"
#include "../records.hpp"
#include "decimal.hpp"
#include "inference_containers.hpp"
#include "inference_values.hpp"
#include <charconv>

namespace clause::semantic::types {
namespace {
// Patterns nested deeper than this match any term as far as their shape goes.
constexpr std::size_t SHAPE_DEPTH = 8;

// A literal size or arity; none for another expression.
std::optional<std::size_t> literal_count(const ast::Module &syntax, const ast::ExprId &id) {
    const auto *literal = std::get_if<ast::IntegerLiteral>(&syntax.expression(ungroup(syntax, id)).value);
    std::size_t value = 0;
    if (!literal) {
        return std::nullopt;
    }
    const auto &digits = literal->value.decimal;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    return parsed.ec == std::errc{} && value <= 255 ? std::optional{value} : std::nullopt;
}

// A tuple of `size` elements whose first element is the atom `name`.
Id record_tuple(Lattice &lattice, const std::u32string &name, const std::size_t size) {
    std::vector<Id> elements(size, lattice.graph().top());
    if (size == 0) {
        return lattice.graph().bottom();
    }
    elements.front() = lattice.atom(utf8(name));
    return lattice.tuple(std::move(elements));
}

// Integer bounds as canonical decimals; a missing bound is unbounded.
struct Interval {
    std::optional<std::string> low;
    std::optional<std::string> high;
};

// Narrowed facts by name, with the integer bounds proven so far in the test: a bound on one side only is no fact of
// its own, so it waits here for the other side.
struct Values {
    BindingValues facts;
    std::map<BindingId, Interval> bounds;
};

// Facts to narrow, starting from `facts`.
Values narrowing(const BindingValues &facts) { return {facts, {}}; }

// The fact of a name in `values`; term() when it has none.
Id current_fact(Lattice &lattice, const Values &values, const BindingId identity) {
    const auto *found = values.facts.find(identity);
    return found ? found->type : lattice.graph().top();
}

// The larger of two lower bounds and the smaller of two upper bounds.
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

// The integer after (`up`) or before a bound; a missing bound stays unbounded.
std::optional<std::string> next(const std::optional<std::string> &bound, const bool up) {
    if (!bound) {
        return std::nullopt;
    }
    const auto value = decimal_number(*bound);
    return decimal_integer(up ? value + 1 : value - 1);
}

// Join into `into` the facts `other` gives the same variables: either of two narrowings may hold.
void merge(Lattice &lattice, Values &into, const Values &other) {
    auto &facts = into.facts.slots();
    for (std::size_t slot = 0; slot < facts.size(); ++slot) {
        auto &fact = facts[slot];
        const auto &second = other.facts.slots()[slot];
        fact = {lattice.join(fact.type, second.type), fact.argument == second.argument ? fact.argument : std::nullopt};
    }
    into.bounds.clear();
}

// Two proven integers, `small` below `large` (`strict`) or at most `large`, with their integer bounds.
struct Ordered {
    BindingId small;
    BindingId large;
    Interval small_bounds;
    Interval large_bounds;
    bool strict;
};

// Narrows variables by tests assumed true.
class Assume final {
  public:
    explicit Assume(BindingFacts &bindings)
        : bindings_(bindings), syntax_(*bindings.function.module->syntax), lattice_(bindings.inference.graph) {}

    // Narrow `values` by a true test; false when it can never be true.
    bool test(const ast::ExprId &id, Values &values);
    // Narrow `values` by a true conjunction of tests.
    bool conjunction(const std::vector<ast::ExprId> &tests, Values &values);
    // Narrow `values` by one of two alternatives: the join of what each proves.
    template <typename Left, typename Right> bool either(Values &values, Left left, Right right);
    // The argument a type test checks and the fact of the values it accepts; none for another expression.
    std::optional<std::pair<ast::ExprId, Id>> type_test(const ast::Expression &expression);

    // The variable, constant and operator (variable first) of a comparison of a variable with an integer constant.
    std::optional<Comparison> comparison(const ast::BinaryExpression &value) const;
    // Narrow by a comparison of a variable with a constant being true.
    bool apply(Values &values, const Comparison &compared);

  private:
    bool binary(const ast::BinaryExpression &value, Values &values);
    // A comparison of two variables proven to be integers: each narrows by the other's bounds.
    bool compare_variables(const ast::BinaryExpression &value, Values &values);
    // Narrow two proven integers (as `small` and `large`) by a comparison of them being true.
    bool relate(Values &values, ast::BinaryOperator operation, const Ordered &pair);
    // Narrow two proven integers known to be ordered.
    bool order(Values &values, const Ordered &ordered);
    // Narrow a proven integer variable to exclude `constant` where it is a bound of its ranges.
    bool exclude(Values &values, BindingId identity, const BigInt &constant);
    // The integer bounds of a variable proven to be an integer; none for another one.
    std::optional<Interval> integer_bounds(const Values &values, BindingId identity);
    // A comparison of a proven integer variable with an integer constant.
    bool compare(const ast::BinaryExpression &value, Values &values);
    // Narrow a variable proven to be an integer to the integers of `range`.
    bool narrow_range(Values &values, BindingId identity, const Interval &range);
    // is_record/2,3: a tuple of the record's size whose first element is its name.
    std::optional<Id> record_test(const ast::Expression &expression, const ast::CallExpression &call);
    // The bounds `operation` against `constant` proves for its left operand; none for others.
    std::optional<Interval> bound(ast::BinaryOperator operation, const BigInt &constant);
    // Narrow one variable by `fact`; false when nothing is left.
    bool narrow(Values &values, BindingId identity, Id fact);
    // The integer constant an expression's recorded fact is.
    std::optional<BigInt> constant(const ast::ExprId &id) const;
    // A type test's category for the test's name.
    std::optional<Id> named_category(const ast::Expression &expression, const ServiceResolution &service);

    BindingFacts &bindings_;
    const ast::Module &syntax_;
    Lattice lattice_;
};

bool Assume::test(const ast::ExprId &id, Values &values) {
    const auto &expression = syntax_.expression(ungroup(syntax_, id));
    if (const auto *value = std::get_if<ast::BinaryExpression>(&expression.value)) {
        return binary(*value, values);
    }
    if (const auto *atom = std::get_if<ast::Atom>(&expression.value)) {
        return atom->name != U"false";
    }
    const auto tested = type_test(expression);
    const auto identity = tested ? variable(bindings_, tested->first) : std::nullopt;
    return !identity || narrow(values, *identity, tested->second);
}

bool Assume::conjunction(const std::vector<ast::ExprId> &tests, Values &values) {
    for (const auto &item : tests) {
        if (!test(item, values)) {
            return false;
        }
    }
    return true;
}

template <typename Left, typename Right> bool Assume::either(Values &values, Left left, Right right) {
    auto first = values;
    auto second = values;
    const bool a = left(first);
    const bool b = right(second);
    if (!a || !b) {
        values = a ? first : second;
        return a || b;
    }
    merge(lattice_, first, second);
    values = first;
    return true;
}

bool Assume::binary(const ast::BinaryExpression &value, Values &values) {
    using Op = ast::BinaryOperator;
    if (value.operation == Op::and_also || value.operation == Op::logical_and) {
        return test(value.left, values) && test(value.right, values);
    }
    if (value.operation == Op::or_else || value.operation == Op::logical_or) {
        return either(
            values, [&](Values &left) { return test(value.left, left); },
            [&](Values &right) { return test(value.right, right); });
    }
    return compare(value, values);
}

std::optional<BigInt> Assume::constant(const ast::ExprId &id) const {
    const auto found = bindings_.inference.expressions.find(&syntax_.expression(id));
    if (found == bindings_.inference.expressions.end()) {
        return std::nullopt;
    }
    const auto &node = bindings_.inference.graph.get(found->second.type);
    return node.kind == Kind::integer ? std::optional{decimal_number(node.name)} : std::nullopt;
}

std::optional<Interval> Assume::bound(const ast::BinaryOperator operation, const BigInt &constant) {
    using Op = ast::BinaryOperator;
    const auto text = [](const BigInt &value) { return std::optional{decimal_integer(value)}; };
    switch (operation) {
    case Op::less:
        return Interval{std::nullopt, text(constant - 1)};
    case Op::less_equal:
        return Interval{std::nullopt, text(constant)};
    case Op::greater:
        return Interval{text(constant + 1), std::nullopt};
    case Op::greater_equal:
        return Interval{text(constant), std::nullopt};
    case Op::equal:
    case Op::exact_equal:
        return Interval{text(constant), text(constant)};
    default:
        return std::nullopt;
    }
}

std::optional<Comparison> Assume::comparison(const ast::BinaryExpression &value) const {
    static const std::map<ast::BinaryOperator, ast::BinaryOperator> FLIPPED{
        {ast::BinaryOperator::less, ast::BinaryOperator::greater},
        {ast::BinaryOperator::less_equal, ast::BinaryOperator::greater_equal},
        {ast::BinaryOperator::greater, ast::BinaryOperator::less},
        {ast::BinaryOperator::greater_equal, ast::BinaryOperator::less_equal},
        {ast::BinaryOperator::equal, ast::BinaryOperator::equal},
        {ast::BinaryOperator::exact_equal, ast::BinaryOperator::exact_equal},
        {ast::BinaryOperator::not_equal, ast::BinaryOperator::not_equal},
        {ast::BinaryOperator::exact_not_equal, ast::BinaryOperator::exact_not_equal}};
    const auto flipped = FLIPPED.find(value.operation);
    if (flipped == FLIPPED.end()) {
        return std::nullopt;
    }
    const auto left = variable(bindings_, value.left);
    const auto number = constant(left ? value.right : value.left);
    const auto identity = left ? left : variable(bindings_, value.right);
    if (!identity || !number) {
        return std::nullopt;
    }
    return Comparison{*identity, *number, left ? value.operation : flipped->second};
}

bool Assume::compare(const ast::BinaryExpression &value, Values &values) {
    const auto compared = comparison(value);
    return compared ? apply(values, *compared) : compare_variables(value, values);
}

bool Assume::apply(Values &values, const Comparison &compared) {
    using Op = ast::BinaryOperator;
    if (compared.operation == Op::not_equal || compared.operation == Op::exact_not_equal) {
        return exclude(values, compared.identity, compared.constant);
    }
    const auto range = bound(compared.operation, compared.constant);
    return !range || narrow_range(values, compared.identity, *range);
}

std::optional<Interval> Assume::integer_bounds(const Values &values, const BindingId identity) {
    const auto numbers = lattice_.numbers(current_fact(lattice_, values, identity));
    if (!numbers.integers || numbers.floats || numbers.others) {
        return std::nullopt;
    }
    return Interval{numbers.low, numbers.high};
}

bool Assume::exclude(Values &values, const BindingId identity, const BigInt &constant) {
    if (!integer_bounds(values, identity)) {
        return true;
    }
    const auto value = decimal_integer(constant);
    std::vector<Id> kept;
    for (const auto member : lattice_.members(values.facts.at(identity).type)) {
        const auto numbers = lattice_.numbers(member);
        // A bound equal to the excluded value moves inward; a singleton equal to it goes.
        const auto low = numbers.low == value ? next(numbers.low, true) : numbers.low;
        const auto high = numbers.high == value ? next(numbers.high, false) : numbers.high;
        if (!low || !high || !decimal_less(*high, *low)) {
            kept.push_back(lattice_.interval(low, high));
        }
    }
    return narrow(values, identity, lattice_.join(kept, 0));
}

bool Assume::compare_variables(const ast::BinaryExpression &value, Values &values) {
    const auto left = variable(bindings_, value.left);
    const auto right = variable(bindings_, value.right);
    const auto a = left ? integer_bounds(values, *left) : std::nullopt;
    const auto b = right ? integer_bounds(values, *right) : std::nullopt;
    return !a || !b || relate(values, value.operation, {*left, *right, *a, *b, false});
}

bool Assume::relate(Values &values, const ast::BinaryOperator operation, const Ordered &pair) {
    using Op = ast::BinaryOperator;
    // Whether each order swaps the operands so the first is the smaller, and whether it is strict.
    static const std::map<Op, std::pair<bool, bool>> ORDERS{{Op::less, {false, true}},
                                                            {Op::less_equal, {false, false}},
                                                            {Op::greater, {true, true}},
                                                            {Op::greater_equal, {true, false}}};
    if (operation == Op::equal || operation == Op::exact_equal) {
        return narrow_range(values, pair.small, pair.large_bounds) &&
               narrow_range(values, pair.large, pair.small_bounds);
    }
    const auto found = ORDERS.find(operation);
    if (found == ORDERS.end()) {
        return true;
    }
    const auto [swapped, strict] = found->second;
    return order(values, swapped ? Ordered{pair.large, pair.small, pair.large_bounds, pair.small_bounds, strict}
                                 : Ordered{pair.small, pair.large, pair.small_bounds, pair.large_bounds, strict});
}

bool Assume::order(Values &values, const Ordered &ordered) {
    // A strict order moves each bound past the other's: small is at most large's highest less one.
    const auto high = ordered.strict ? next(ordered.large_bounds.high, false) : ordered.large_bounds.high;
    const auto low = ordered.strict ? next(ordered.small_bounds.low, true) : ordered.small_bounds.low;
    return narrow_range(values, ordered.small, {std::nullopt, high}) &&
           narrow_range(values, ordered.large, {low, std::nullopt});
}

bool Assume::narrow_range(Values &values, const BindingId identity, const Interval &range) {
    const auto numbers = lattice_.numbers(current_fact(lattice_, values, identity));
    // Terms of every type compare: only a value proven to be an integer narrows to a range.
    if (!numbers.integers || numbers.floats || numbers.others) {
        return true;
    }
    // Intersect the bounds directly, with those the test proved before: a range bounded on one side only is no
    // fact of its own.
    const auto proven = values.bounds.contains(identity) ? values.bounds.at(identity) : Interval{};
    const auto low = higher(higher(numbers.low, proven.low), range.low);
    const auto high = lower(lower(numbers.high, proven.high), range.high);
    values.bounds.insert_or_assign(identity, Interval{low, high});
    const bool empty = low && high && decimal_less(*high, *low);
    return narrow(values, identity, empty ? lattice_.graph().bottom() : lattice_.interval(low, high));
}

bool Assume::narrow(Values &values, const BindingId identity, const Id fact) {
    const auto current = current_fact(lattice_, values, identity);
    const auto next = bindings_.narrow_identity(values.facts, identity, fact);
    return next != lattice_.graph().bottom() || current == lattice_.graph().bottom();
}

std::optional<Id> Assume::named_category(const ast::Expression &expression, const ServiceResolution &service) {
    static const std::map<std::u32string, std::string_view> CATEGORIES{
        {U"is_atom", "atom"},           {U"is_boolean", "boolean"},
        {U"is_integer", "integer"},     {U"is_float", "float"},
        {U"is_number", "number"},       {U"is_binary", "binary"},
        {U"is_bitstring", "bitstring"}, {U"is_list", "maybe_improper_list"},
        {U"is_tuple", "tuple"},         {U"is_map", "map"},
        {U"is_function", "fun"},        {U"is_pid", "pid"},
        {U"is_port", "port"},           {U"is_reference", "reference"}};
    const auto &call = std::get<ast::CallExpression>(expression.value);
    const auto &name = service.identity.name;
    if (const auto found = CATEGORIES.find(name); found != CATEGORIES.end() && service.identity.arity == 1) {
        return lattice_.category(found->second);
    }
    if (name == U"is_function" && service.identity.arity == 2) {
        const auto arity = literal_count(syntax_, call.arguments[1]);
        return arity ? std::optional{lattice_.fun(*arity, lattice_.graph().top())} : std::nullopt;
    }
    return name == U"is_record" && service.identity.arity >= 2 ? record_test(expression, call) : std::nullopt;
}

std::optional<Id> Assume::record_test(const ast::Expression &expression, const ast::CallExpression &call) {
    const auto *record = std::get_if<ast::Atom>(&syntax_.expression(ungroup(syntax_, call.arguments[1])).value);
    const auto *layout = record ? record_layout(*bindings_.function.module, *record, expression.source) : nullptr;
    const auto size = call.arguments.size() == 3 ? literal_count(syntax_, call.arguments[2])
                                                 : (layout ? std::optional{layout->fields.size() + 1} : std::nullopt);
    return record && size ? std::optional{record_tuple(lattice_, record->name, *size)} : std::nullopt;
}

std::optional<std::pair<ast::ExprId, Id>> Assume::type_test(const ast::Expression &expression) {
    const auto *call = std::get_if<ast::CallExpression>(&expression.value);
    const auto &services = bindings_.function.function->services;
    const auto service = call ? services.find(&expression) : services.end();
    if (service == services.end() || call->arguments.empty()) {
        return std::nullopt;
    }
    if (service->second.identity.name == U"is_map_key" && call->arguments.size() == 2) {
        return std::pair{call->arguments[1], lattice_.category("map")};
    }
    const auto category = named_category(expression, service->second);
    return category ? std::optional{std::pair{call->arguments[0], *category}} : std::nullopt;
}

// Builds the shape of the values a pattern matches.
class Shape final {
  public:
    explicit Shape(BindingFacts &bindings)
        : bindings_(bindings), syntax_(*bindings.function.module->syntax), lattice_(bindings.inference.graph) {}

    // The shape of a pattern `depth` levels deep.
    Id of(const ast::ExprId &id, std::size_t depth);

  private:
    // Groups, aliases, tuples, lists and records; none for other patterns.
    std::optional<Id> composite(const ast::ExprValue &value, std::size_t depth);
    // Maps, bitstrings, variables and literals.
    Id leaf(const ast::Expression &expression);
    Id tuple(const ast::Tuple &value, std::size_t depth);
    Id list(const ast::List &value, std::size_t depth);
    Id record(const ast::RecordExpression &value, std::size_t depth);
    // A variable: a bound one's fact, any term for a new one.
    Id variable(const ast::Expression &expression);

    BindingFacts &bindings_;
    const ast::Module &syntax_;
    Lattice lattice_;
};

Id Shape::of(const ast::ExprId &id, const std::size_t depth) {
    const auto &expression = syntax_.expression(id);
    if (depth > SHAPE_DEPTH) {
        return lattice_.graph().top();
    }
    if (const auto shape = composite(expression.value, depth)) {
        return *shape;
    }
    return leaf(expression);
}

std::optional<Id> Shape::composite(const ast::ExprValue &value, const std::size_t depth) {
    if (const auto *group = std::get_if<ast::Group>(&value)) {
        return of(group->expression, depth);
    }
    if (const auto *match = std::get_if<ast::MatchExpression>(&value)) {
        return lattice_.meet(of(match->left, depth), of(match->right, depth));
    }
    if (const auto *tuple_value = std::get_if<ast::Tuple>(&value)) {
        return tuple(*tuple_value, depth);
    }
    if (const auto *list_value = std::get_if<ast::List>(&value)) {
        return list(*list_value, depth);
    }
    if (const auto *record_value = std::get_if<ast::RecordExpression>(&value)) {
        return record(*record_value, depth);
    }
    return std::nullopt;
}

Id Shape::leaf(const ast::Expression &expression) {
    const auto &value = expression.value;
    if (std::holds_alternative<ast::MapExpression>(value) || std::holds_alternative<ast::Bitstring>(value)) {
        return lattice_.category(std::holds_alternative<ast::MapExpression>(value) ? "map" : "bitstring");
    }
    if (std::holds_alternative<ast::Variable>(value)) {
        return variable(expression);
    }
    const bool literal =
        std::holds_alternative<ast::Atom>(value) || std::holds_alternative<ast::IntegerLiteral>(value) ||
        std::holds_alternative<ast::CharacterLiteral>(value) || std::holds_alternative<ast::FloatLiteral>(value) ||
        std::holds_alternative<ast::StringLiteral>(value);
    const auto fact = literal ? constructed_fact(bindings_.inference, bindings_.function, value) : std::nullopt;
    return fact.value_or(lattice_.graph().top());
}

Id Shape::tuple(const ast::Tuple &value, const std::size_t depth) {
    std::vector<Id> elements;
    elements.reserve(value.elements.size());
    for (const auto &element : value.elements) {
        elements.push_back(of(element, depth + 1));
    }
    return lattice_.tuple(std::move(elements));
}

Id Shape::list(const ast::List &value, const std::size_t depth) {
    if (value.elements.empty()) {
        return value.tail ? of(*value.tail, depth) : lattice_.nil();
    }
    std::vector<Id> elements;
    elements.reserve(value.elements.size());
    for (const auto &element : value.elements) {
        elements.push_back(of(element, depth + 1));
    }
    return cons(lattice_, {lattice_.join(elements, 0), value.tail ? of(*value.tail, depth + 1) : lattice_.nil()});
}

Id Shape::record(const ast::RecordExpression &value, const std::size_t depth) {
    const auto *layout = record_layout(*bindings_.function.module, value.identity);
    if (!layout || layout->native) {
        return lattice_.graph().top();
    }
    std::vector<Id> elements{lattice_.atom(utf8(layout->name.name))};
    for (const auto &field : record_values(*bindings_.function.module, value, true)) {
        elements.push_back(field ? of(*field, depth + 1) : lattice_.graph().top());
    }
    return lattice_.tuple(std::move(elements));
}

Id Shape::variable(const ast::Expression &expression) {
    const auto event = bindings_.events.find(&expression);
    if (event == bindings_.events.end() || event->second->use == BindingUse::definition) {
        return lattice_.graph().top();
    }
    // A bound variable or a repeated one matches exactly the value it holds.
    const auto *found = bindings_.values.find(event->second->identity);
    return found ? found->type : lattice_.graph().top();
}

// Whether a pattern node matches every value of its own shape: a literal atom or integer, [], a new variable or `_`;
// none for groups and tuples, whose parts decide.
std::optional<bool> exact_leaf(const BindingFacts &bindings, const ast::Expression &expression) {
    const auto &value = expression.value;
    if (std::holds_alternative<ast::Group>(value) || std::holds_alternative<ast::Tuple>(value)) {
        return std::nullopt;
    }
    if (std::holds_alternative<ast::Variable>(value)) {
        const auto event = bindings.events.find(&expression);
        return event == bindings.events.end() || event->second->use == BindingUse::definition;
    }
    const auto *list = std::get_if<ast::List>(&value);
    return std::holds_alternative<ast::Atom>(value) || std::holds_alternative<ast::IntegerLiteral>(value) ||
           std::holds_alternative<ast::CharacterLiteral>(value) || (list && list->elements.empty() && !list->tail);
}
} // namespace

std::optional<BindingId> variable(const BindingFacts &bindings, const ast::ExprId &expression) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto event = bindings.events.find(&syntax.expression(ungroup(syntax, expression)));
    return event == bindings.events.end() ? std::nullopt : std::optional{event->second->identity};
}

bool assume(BindingFacts &bindings, const ast::ExprId &test) {
    auto values = narrowing(bindings.values);
    const bool possible = Assume(bindings).test(test, values);
    bindings.values = std::move(values.facts);
    return possible;
}

bool assume_guard(BindingFacts &bindings, const ast::GuardSyntax &guard) {
    Assume assumption(bindings);
    std::optional<Values> result;
    bool possible = false;
    for (const auto &alternative : guard.alternatives) {
        auto values = narrowing(bindings.values);
        if (!assumption.conjunction(alternative.tests, values)) {
            continue;
        }
        possible = true;
        if (!result) {
            result = values;
            continue;
        }
        Lattice lattice(bindings.inference.graph);
        merge(lattice, *result, values);
    }
    if (result) {
        bindings.values = std::move(result->facts);
    }
    return possible;
}

Id pattern_shape(BindingFacts &bindings, const ast::ExprId &pattern) { return Shape(bindings).of(pattern, 0); }

bool exact_shape(const BindingFacts &bindings, const ast::ExprId &pattern) {
    const auto &syntax = *bindings.function.module->syntax;
    std::vector<ast::ExprId> pending{pattern};
    for (std::size_t visited = 0; !pending.empty(); ++visited) {
        const auto &expression = syntax.expression(pending.back());
        pending.pop_back();
        if (const auto exact = exact_leaf(bindings, expression)) {
            if (!*exact) {
                return false;
            }
        } else if (const auto *tuple = std::get_if<ast::Tuple>(&expression.value)) {
            pending.insert(pending.end(), tuple->elements.begin(), tuple->elements.end());
        } else {
            pending.push_back(std::get<ast::Group>(expression.value).expression);
        }
        if (visited > SHAPE_DEPTH * SHAPE_DEPTH) {
            return false;
        }
    }
    return true;
}

std::optional<Comparison> single_comparison(BindingFacts &bindings, const ast::GuardSyntax &guard) {
    if (guard.alternatives.size() != 1 || guard.alternatives.front().tests.size() != 1) {
        return std::nullopt;
    }
    const auto &syntax = *bindings.function.module->syntax;
    const auto &test = syntax.expression(ungroup(syntax, guard.alternatives.front().tests.front())).value;
    const auto *compared = std::get_if<ast::BinaryExpression>(&test);
    return compared ? Assume(bindings).comparison(*compared) : std::nullopt;
}

void assume_false(BindingFacts &bindings, const Comparison &compared, const BindingId target) {
    using Op = ast::BinaryOperator;
    static const std::map<Op, Op> NEGATED{{Op::less, Op::greater_equal},    {Op::less_equal, Op::greater},
                                          {Op::greater, Op::less_equal},    {Op::greater_equal, Op::less},
                                          {Op::equal, Op::not_equal},       {Op::exact_equal, Op::not_equal},
                                          {Op::not_equal, Op::exact_equal}, {Op::exact_not_equal, Op::exact_equal}};
    auto values = narrowing(bindings.values);
    (void)Assume(bindings).apply(values, {target, compared.constant, NEGATED.at(compared.operation)});
    bindings.values = std::move(values.facts);
}

std::optional<std::pair<BindingId, Id>> single_test(BindingFacts &bindings, const ast::GuardSyntax &guard) {
    if (guard.alternatives.size() != 1 || guard.alternatives.front().tests.size() != 1) {
        return std::nullopt;
    }
    const auto &syntax = *bindings.function.module->syntax;
    const auto &test = syntax.expression(ungroup(syntax, guard.alternatives.front().tests.front()));
    const auto tested = Assume(bindings).type_test(test);
    const auto identity = tested ? variable(bindings, tested->first) : std::nullopt;
    return identity ? std::optional{std::pair{*identity, tested->second}} : std::nullopt;
}
} // namespace clause::semantic::types

namespace clause::semantic::types {
namespace {
// Whether a guard test holds for every value of the facts it narrows: `true`, or a type test of a name of
// `arguments` (is_map_key/2 also needs the key, so it is not).
bool exact_test(BindingFacts &bindings, const ast::ExprId &test, const std::set<BindingId> &arguments) {
    const auto &syntax = *bindings.function.module->syntax;
    const auto &expression = syntax.expression(ungroup(syntax, test));
    if (const auto *atom = std::get_if<ast::Atom>(&expression.value)) {
        return atom->name == U"true";
    }
    const auto service = bindings.function.function->services.find(&expression);
    if (service != bindings.function.function->services.end() && service->second.identity.name == U"is_map_key") {
        return false;
    }
    const auto tested = Assume(bindings).type_test(expression);
    const auto identity = tested ? variable(bindings, tested->first) : std::nullopt;
    return identity && arguments.contains(*identity);
}
} // namespace

bool exact_guard(BindingFacts &bindings, const ast::GuardSyntax *guard, const std::set<BindingId> &arguments) {
    if (!guard) {
        return true;
    }
    return guard->alternatives.size() == 1 &&
           std::ranges::all_of(guard->alternatives.front().tests,
                               [&](const ast::ExprId &test) { return exact_test(bindings, test, arguments); });
}
} // namespace clause::semantic::types
