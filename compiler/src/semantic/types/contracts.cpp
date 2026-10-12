#include "contracts.hpp"
#include "../features.hpp"
#include "lattice.hpp"
#include "printing.hpp"
#include <algorithm>
#include <charconv>
#include <set>

// Specifications checked against inference (docs/semantic.md#inference): a declared type is turned into the facts
// it holds (or more), and a spec contradicts the code when the facts share no value with what inference proves of
// a result, an entry domain or a call's arguments. Unknown facts never contradict anything.
namespace clause::semantic::types {
namespace {
// Declared types nested deeper than this hold any term as far as the check goes.
constexpr std::size_t DECLARED_DEPTH = 16;

// Turns declared types into inference facts that hold every value the type does.
class Declared final {
  public:
    Declared(Registry &declared, Lattice &lattice, const std::string &owner)
        : declared_(declared), lattice_(lattice), top_(lattice.graph().top()), owner_(owner) {}

    // The facts of a declared type, type variables bounded by `constraints` (unconstrained ones hold any term).
    Id fact(Id type, const std::vector<std::pair<std::string, Id>> &constraints) {
        constraints_ = &constraints;
        expanding_.clear();
        return convert(type, 0);
    }

  private:
    Id convert(Id type, std::size_t depth);

    // One rule per kind of declared type node.
    Id nothing(Id, const Node &, std::size_t) { return lattice_.graph().bottom(); }

    Id atom(Id, const Node &node, std::size_t) { return lattice_.atom(node.name); }

    Id integer(Id, const Node &node, std::size_t) { return lattice_.integer(node.name); }

    Id range(Id, const Node &node, std::size_t);

    Id members(Id, const Node &node, std::size_t depth) { return lattice_.join(children(node, depth), 0); }

    Id tuple(Id, const Node &node, std::size_t depth);
    Id list(Id, const Node &node, std::size_t depth);

    Id map(Id, const Node &, std::size_t) { return lattice_.category("map"); }

    Id record(Id, const Node &, std::size_t) { return lattice_.category("tuple"); }

    Id annotated(Id, const Node &node, std::size_t depth) { return convert(node.children.back(), depth); }

    // Built-in types by name and arguments: constants, list types and categories.
    Id named(Id, const Node &node, std::size_t depth);
    std::optional<Id> constant(const std::string &name);
    std::optional<Id> listed(const std::string &name, const std::vector<Id> &arguments);
    Id category(const std::string &name, const std::vector<Id> &arguments);
    // A user type: its definition where it is visible, else any term.
    Id reference(Id type, const Node &, std::size_t depth);
    Id bitstring(Id, const Node &node, std::size_t);
    Id variable(Id, const Node &node, std::size_t depth);
    Id function(Id, const Node &node, std::size_t depth);

    // The converted children of a node.
    std::vector<Id> children(const Node &node, std::size_t depth) {
        std::vector<Id> result;
        result.reserve(node.children.size());
        for (const auto child : node.children) {
            result.push_back(convert(child, depth + 1));
        }
        return result;
    }

    Registry &declared_;
    Lattice &lattice_;
    Id top_;
    const std::string &owner_;
    const std::vector<std::pair<std::string, Id>> *constraints_ = nullptr;
    // References being expanded: a recursive type holds any term past its first layer.
    std::set<Id> expanding_;
};

Id Declared::convert(const Id type, const std::size_t depth) {
    using Rule = Id (Declared::*)(Id, const Node &, std::size_t);
    static const std::map<Kind, Rule> RULES{
        {Kind::bottom, &Declared::nothing},      {Kind::atom, &Declared::atom},
        {Kind::integer, &Declared::integer},     {Kind::range, &Declared::range},
        {Kind::application, &Declared::named},   {Kind::reference, &Declared::reference},
        {Kind::union_type, &Declared::members},  {Kind::tuple, &Declared::tuple},
        {Kind::list, &Declared::list},           {Kind::map, &Declared::map},
        {Kind::record, &Declared::record},       {Kind::bitstring, &Declared::bitstring},
        {Kind::function, &Declared::function},   {Kind::variable, &Declared::variable},
        {Kind::annotation, &Declared::annotated}};
    const auto node = declared_.graph.get(type);
    const auto rule = RULES.find(node.kind);
    return depth > DECLARED_DEPTH || rule == RULES.end() ? top_ : (this->*rule->second)(type, node, depth);
}

Id Declared::range(Id, const Node &node, std::size_t) {
    return lattice_.range(
        {declared_.graph.get(node.children.at(0)).name, declared_.graph.get(node.children.at(1)).name});
}

Id Declared::tuple(Id, const Node &node, const std::size_t depth) {
    return node.name == "any" ? lattice_.category("tuple") : lattice_.tuple(children(node, depth));
}

Id Declared::list(Id, const Node &node, const std::size_t depth) {
    return node.children.empty() ? lattice_.nil()
                                 : lattice_.list(convert(node.children[0], depth + 1), node.name == "nonempty");
}

Id Declared::named(Id, const Node &node, const std::size_t depth) {
    if (node.module != "erlang") {
        return top_;
    }
    const auto arguments = children(node, depth);
    if (const auto fact = constant(node.name)) {
        return *fact;
    }
    if (const auto fact = listed(node.name, arguments)) {
        return *fact;
    }
    return category(node.name, arguments);
}

std::optional<Id> Declared::constant(const std::string &name) {
    static const std::map<std::string_view, std::pair<std::string_view, std::string_view>> RANGES{
        {"byte", {"0", "255"}}, {"arity", {"0", "255"}}, {"char", {"0", "1114111"}}};
    if (name == "none" || name == "no_return") {
        return lattice_.graph().bottom();
    }
    if (const auto found = RANGES.find(name); found != RANGES.end()) {
        return lattice_.range({found->second.first, found->second.second});
    }
    if (name == "timeout") {
        return lattice_.join(lattice_.category("non_neg_integer"), lattice_.atom("infinity"));
    }
    if (name == "mfa") {
        return lattice_.tuple({lattice_.category("atom"), lattice_.category("atom"), lattice_.range({"0", "255"})});
    }
    return name == "nil" ? std::optional{lattice_.nil()} : std::nullopt;
}

std::optional<Id> Declared::listed(const std::string &name, const std::vector<Id> &arguments) {
    if (name == "list" || name == "nonempty_list") {
        return lattice_.list(arguments.empty() ? top_ : arguments[0], name == "nonempty_list");
    }
    if (name == "nonempty_improper_list" && arguments.size() == 2) {
        return lattice_.improper(arguments[0], arguments[1]);
    }
    return std::nullopt;
}

Id Declared::category(const std::string &name, const std::vector<Id> &arguments) {
    static const std::set<std::string_view> CATEGORIES{"integer",
                                                       "pos_integer",
                                                       "non_neg_integer",
                                                       "neg_integer",
                                                       "float",
                                                       "number",
                                                       "atom",
                                                       "boolean",
                                                       "pid",
                                                       "port",
                                                       "reference",
                                                       "binary",
                                                       "bitstring",
                                                       "nonempty_binary",
                                                       "nonempty_bitstring",
                                                       "tuple",
                                                       "map",
                                                       "string",
                                                       "nonempty_string"};
    // Categories an alias or a list of any shape stands for.
    static const std::map<std::string_view, std::vector<std::string_view>> UNIONS{
        {"module", {"atom"}},
        {"node", {"atom"}},
        {"identifier", {"pid", "port", "reference"}},
        {"iolist", {"maybe_improper_list"}},
        {"iodata", {"maybe_improper_list", "binary"}},
        {"maybe_improper_list", {"maybe_improper_list"}},
        {"nonempty_maybe_improper_list", {"nonempty_maybe_improper_list"}},
        {"fun", {"fun"}},
        {"function", {"fun"}}};
    if (CATEGORIES.contains(name) && arguments.empty()) {
        return lattice_.category(name);
    }
    const auto found = UNIONS.find(name);
    if (found == UNIONS.end()) {
        return top_;
    }
    std::vector<Id> members;
    members.reserve(found->second.size());
    for (const auto member : found->second) {
        members.push_back(lattice_.category(member));
    }
    return lattice_.join(members, 0);
}

Id Declared::reference(const Id type, const Node &, const std::size_t depth) {
    if (!expanding_.insert(type).second) {
        return top_;
    }
    const auto expanded = expand_reference(declared_, type, owner_);
    const auto result = expanded ? convert(*expanded, depth + 1) : top_;
    expanding_.erase(type);
    return result;
}

Id Declared::bitstring(Id, const Node &node, std::size_t) {
    std::uint64_t base = 0;
    std::uint64_t unit = 0;
    for (std::size_t index = 0; index < node.children.size() && index < node.labels.size(); ++index) {
        const auto &digits = declared_.graph.get(node.children[index]).name;
        std::uint64_t value = 0;
        if (std::from_chars(digits.data(), digits.data() + digits.size(), value).ec != std::errc{}) {
            return lattice_.category("bitstring");
        }
        (node.labels[index] == "unit" ? unit : base) = value;
    }
    return lattice_.bitstring(base, unit);
}

Id Declared::variable(Id, const Node &node, const std::size_t depth) {
    const auto found = std::ranges::find_if(*constraints_, [&](const auto &bound) { return bound.first == node.name; });
    if (found == constraints_->end()) {
        return top_;
    }
    // A constrained variable holds its bound; the bound itself is read without constraints, as written.
    const auto *constraints = constraints_;
    const std::vector<std::pair<std::string, Id>> none;
    constraints_ = &none;
    const auto result = convert(found->second, depth + 1);
    constraints_ = constraints;
    return result;
}

Id Declared::function(Id, const Node &node, const std::size_t depth) {
    if (!std::ranges::contains(node.labels, std::string("result")) || node.name == "any_arguments") {
        return lattice_.category("fun");
    }
    return lattice_.fun(node.children.size() - 1, convert(node.children.back(), depth + 1));
}

// A function's specification; callbacks are not checked against implementations.
const Contract *contract_for(const Registry &declared, const FunctionRef function) {
    const Key key{utf8(function.module->name), utf8(function.function->key.name), function.function->key.arity};
    const auto found = std::ranges::find_if(
        declared.contracts, [&](const auto &contract) { return !contract.callback && contract.key == key; });
    return found == declared.contracts.end() ? nullptr : &*found;
}

// The declared argument and result types of an overload; none for a malformed one.
std::optional<std::vector<Id>> signature(const Registry &declared, const Overload &overload) {
    const auto &node = declared.graph.get(overload.function);
    if (node.kind != Kind::function || node.children.empty() || node.name == "any_arguments") {
        return std::nullopt;
    }
    return node.children;
}

// Checks one batch's specifications against its inferred facts.
class Checker final {
  public:
    Checker(Registry &declared, Inference &inferred, const Reporter &out)
        : declared_(declared), inferred_(inferred), lattice_(inferred.graph), out_(out) {}

    void result(FunctionRef function);
    void arguments(FunctionRef function);
    void call(const Call &call);

  private:
    // The inferred facts of a call's arguments.
    std::vector<Id> argument_facts(const Call &call) const;
    // The first argument an overload rejects (its declared type as written and the inferred fact), if any.
    std::optional<std::pair<std::string, Id>> rejection(const std::string &caller, const Overload &overload,
                                                        const std::vector<Id> &facts);

    // Whether an inferred fact and a declared one share no value; unknown and never-produced facts never contradict.
    bool disjoint(Id inferred, Id declared) {
        const auto &graph = inferred_.graph;
        return inferred != graph.top() && inferred != graph.bottom() &&
               lattice_.meet(inferred, declared) == graph.bottom();
    }

    // The fact of an overload's declared type at `index` (the result is the last), as module `viewer` sees it: an
    // opaque type of another module is any term there.
    Id declared_fact(const std::string &viewer, const Overload &overload, std::size_t index) {
        const auto types = signature(declared_, overload);
        if (!types || index >= types->size()) {
            return inferred_.graph.top();
        }
        return Declared(declared_, lattice_, viewer).fact(types->at(index), overload.constraints);
    }

    // The declared type of an overload at `index` (the result is the last) as written.
    std::string declared_text(const Overload &overload, std::size_t index) const {
        const auto types = signature(declared_, overload);
        auto text =
            types && index < types->size() ? type_source(declared_.graph, types->at(index)) : std::string(TERM_SOURCE);
        std::string constraints;
        for (const auto &[variable, bound] : overload.constraints) {
            constraints +=
                (constraints.empty() ? " when " : ", ") + variable + " :: " + type_source(declared_.graph, bound);
        }
        return text + constraints;
    }

    // Report a contradiction at a specification or call: the declared and the inferred type.
    void contradiction(const Module &module, const ast::NodeSource &source, const std::string &what,
                       const std::string &declared, Id inferred, const Severity severity = Severity::error) {
        report(module, &source,
               what + ": declared " + declared + ", inferred " + type_source(inferred_.graph, inferred), out_,
               severity);
    }

    Registry &declared_;
    Inference &inferred_;
    Lattice lattice_;
    const Reporter &out_;
};

void Checker::result(const FunctionRef function) {
    const auto *contract = contract_for(declared_, function);
    if (!contract) {
        return;
    }
    const auto inferred = inferred_.functions.at(function.function).result.type;
    std::vector<Id> results;
    std::string text;
    const auto arity = function.function->key.arity;
    for (const auto &overload : contract->overloads) {
        results.push_back(declared_fact(contract->key.module, overload, arity));
        text += (text.empty() ? "" : " | ") + declared_text(overload, arity);
    }
    // A warning: the program keeps the inferred result and never relies on the specification.
    if (disjoint(inferred, lattice_.join(results, 0))) {
        contradiction(*function.module, function.module->syntax->form(contract->form).source,
                      "inferred result contradicts specification for " + contract->key.name, text, inferred,
                      Severity::warning);
    }
}

void Checker::arguments(const FunctionRef function) {
    const auto *contract = contract_for(declared_, function);
    if (!contract) {
        return;
    }
    const auto &entry = inferred_.functions.at(function.function).entry;
    for (const auto &overload : contract->overloads) {
        for (std::size_t index = 0; index < entry.size(); ++index) {
            const auto declared = declared_fact(contract->key.module, overload, index);
            if (disjoint(entry[index], declared)) {
                contradiction(*function.module, overload.source,
                              "argument " + std::to_string(index + 1) + " of specification for " + contract->key.name +
                                  " can never be accepted",
                              declared_text(overload, index), entry[index]);
                return;
            }
        }
    }
}

std::vector<Id> Checker::argument_facts(const Call &call) const {
    const auto &syntax = *call.caller.module->syntax;
    const auto &arguments = std::get<ast::CallExpression>(syntax.expression(call.expression).value).arguments;
    std::vector<Id> facts;
    facts.reserve(arguments.size());
    for (const auto &argument : arguments) {
        const auto found = inferred_.expressions.find(&syntax.expression(argument));
        facts.push_back(found == inferred_.expressions.end() ? inferred_.graph.top() : found->second.type);
    }
    return facts;
}

std::optional<std::pair<std::string, Id>> Checker::rejection(const std::string &caller, const Overload &overload,
                                                             const std::vector<Id> &facts) {
    for (std::size_t index = 0; index < facts.size(); ++index) {
        if (disjoint(facts[index], declared_fact(caller, overload, index))) {
            return std::pair{declared_text(overload, index), facts[index]};
        }
    }
    return std::nullopt;
}

void Checker::call(const Call &call) {
    const auto *contract = contract_for(declared_, call.callee);
    if (!contract) {
        return;
    }
    const auto caller = utf8(call.caller.module->name);
    const auto facts = argument_facts(call);
    // The call contradicts the specification when every overload rejects one of its arguments.
    std::vector<std::pair<std::string, Id>> rejected;
    for (const auto &overload : contract->overloads) {
        if (auto reason = rejection(caller, overload, facts)) {
            rejected.push_back(std::move(*reason));
        }
    }
    // A warning: the call may sit on a path that never runs, and code never relies on the callee's specification.
    if (!rejected.empty() && rejected.size() == contract->overloads.size()) {
        const auto &syntax = *call.caller.module->syntax;
        contradiction(*call.caller.module, syntax.expression(call.expression).source,
                      "inferred arguments contradict specification for " + contract->key.name, rejected.front().first,
                      rejected.front().second, Severity::warning);
    }
}
} // namespace

void check_contracts(Registry &declared, Inference &inferred, const CallGraph &calls, const Reporter &out) {
    Checker checker(declared, inferred, out);
    for (const auto function : calls.order) {
        checker.result(function);
        checker.arguments(function);
    }
    for (const auto &call : calls.calls) {
        checker.call(call);
    }
}
} // namespace clause::semantic::types
