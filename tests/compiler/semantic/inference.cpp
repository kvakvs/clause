#include "semantic/types/inference.hpp"
#include "semantic/bindings.hpp"
#include "semantic/capabilities.hpp"
#include "semantic/services.hpp"
#include "semantic/types/declarations.hpp"
#include "semantic/types/lattice.hpp"
#include "semantic/types/printing.hpp"
#include <algorithm>
#include <clause/compiler/parser.hpp>
#include <cstdio>
#include <initializer_list>
#include <source_location>
#include <stdexcept>
using namespace clause;
using namespace clause::semantic;
namespace t = clause::semantic::types;

// Keep these relational and budget invariants until public type inspection replaces them.
void require(bool value, const std::source_location site = std::source_location::current()) {
    if (!value) {
        throw std::runtime_error("inference invariant at line " + std::to_string(site.line()));
    }
}

// Real source proves annotations stay independent of constants and parameter projections.
void local_inference() {
    SourceManager sources;
    PreprocessorSession pp(sources.add("facts.erl", R"(
-module(facts).
-export([constant/0,id/1,project/3,unknown/1]).
-spec constant() -> atom().
constant() -> -42.
id(X) -> (X).
-spec project(integer(),atom(),term()) -> atom().
project(_,Y,_) -> Y.
unknown(X) -> id(X).
)"));
    auto parsed = parse_module(pp);
    require(!parsed.failed);
    const Reporter report = [](const Diagnostic &d) { require(d.severity != Severity::error); };
    std::vector<std::unique_ptr<Module>> modules;
    modules.push_back(index(parsed.module, "facts.erl", report));
    auto &module = *modules.front();
    bind_parameters(module, report);
    resolve_services(module, report);
    check_capabilities(module, report);
    const auto calls = resolve_calls(modules, report);
    const auto declared = t::resolve_declarations(modules, report);
    const auto inferred = t::infer(calls);
    require(declared->contracts.size() == 2 && !inferred->graph.widened());
    const auto &constant = inferred->functions.at(&module.functions[0]);
    require(inferred->graph.get(constant.result.type).name == "-42" && !constant.result.argument);
    const auto &identity = inferred->functions.at(&module.functions[1]);
    require(identity.inputs == std::vector{inferred->graph.top()});
    require(identity.result.type == inferred->graph.top() && identity.result.argument == 0);
    const auto &projection = inferred->functions.at(&module.functions[2]);
    require(projection.inputs == std::vector(3, inferred->graph.top()) && projection.result.argument == 1);
    require(inferred->functions.at(&module.functions[3]).result.argument == 0);
    const auto bounded = t::infer(calls, {.syntax_work = 0});
    require(bounded->graph.widened());
    for (const auto &[function, summary] : bounded->functions) {
        require(function != nullptr && summary.result.type == bounded->graph.top() && !summary.result.argument);
    }
    const auto node_limited = t::infer(calls, {.nodes = 2});
    require(node_limited->graph.widened());
    require(node_limited->functions.at(&module.functions[0]).result.type == node_limited->graph.top());
}

// The printed fact of the join of every fact in `facts`.
std::string joined(t::Lattice &lattice, std::initializer_list<t::Id> facts) {
    auto result = lattice.graph().bottom();
    for (const auto fact : facts) {
        result = lattice.join(result, fact);
    }
    return t::type_source(lattice.graph(), result);
}

// Require that `actual` prints as `expected`, naming both when it does not.
void prints(const std::string &actual, const std::string &expected,
            const std::source_location site = std::source_location::current()) {
    if (actual != expected) {
        throw std::runtime_error("line " + std::to_string(site.line()) + ": expected " + expected + ", got " + actual);
    }
}

// Integers stay singletons up to the budget, then become their range; unbounded intervals print as categories.
void integer_joins() {
    t::Graph graph;
    t::Lattice lattice(graph);
    const auto i = [&](int value) { return lattice.integer(std::to_string(value)); };
    prints(joined(lattice, {i(3), i(1), i(2), i(1)}), "1..3");
    prints(joined(lattice, {i(7), i(1), i(3)}), "1 | 3 | 7");
    prints(joined(lattice, {i(1), i(2), i(3), i(4), i(5), i(6), i(7), i(8)}), "1..8");
    prints(joined(lattice, {i(1), i(2), i(3), i(4), i(5), i(6), i(7), i(8), i(20)}), "1..20");
    prints(joined(lattice, {lattice.range({"1", "10"}), i(-3)}), "-3..10");
    prints(joined(lattice, {lattice.range({"0", "255"}), lattice.category("pos_integer")}), "non_neg_integer()");
    prints(joined(lattice, {lattice.category("neg_integer"), i(-7)}), "neg_integer()");
    prints(joined(lattice, {lattice.category("neg_integer"), i(0)}), "integer()");
    prints(joined(lattice, {i(1), lattice.category("float")}), "1 | float()");
    prints(joined(lattice, {lattice.range({"1", "2"}), lattice.category("float")}), "number()");
    prints(joined(lattice, {lattice.category("number"), i(5)}), "number()");
}

// Atoms stay singletons up to the budget; true and false print as boolean(); identifiers are categories.
void atom_joins() {
    t::Graph graph;
    t::Lattice lattice(graph);
    prints(joined(lattice, {lattice.atom("ok"), lattice.atom("error")}), "error | ok");
    prints(joined(lattice, {lattice.atom("true"), lattice.atom("false")}), "boolean()");
    prints(joined(lattice, {lattice.category("boolean"), lattice.atom("unknown")}), "false | true | unknown");
    std::vector<t::Id> nine;
    for (const auto *name : {"a", "b", "c", "d", "e", "f", "g", "h", "i"}) {
        nine.push_back(lattice.atom(name));
    }
    prints(joined(lattice, {nine[0], nine[1], nine[2], nine[3], nine[4], nine[5], nine[6], nine[7], nine[8]}),
           "atom()");
    prints(joined(lattice, {lattice.category("pid"), lattice.category("reference")}), "reference() | pid()");
    prints(joined(lattice, {lattice.atom("infinity"), lattice.category("non_neg_integer")}),
           "non_neg_integer() | infinity");
    prints(joined(lattice, {graph.top(), lattice.atom("ok")}), "_");
    prints(joined(lattice, {graph.bottom(), lattice.atom("ok")}), "ok");
}

// Tuples join element by element when size and tag agree; lists, maps, funs and bitstrings by their rules.
void container_joins() {
    t::Graph graph;
    t::Lattice lattice(graph);
    const auto ok = lattice.atom("ok");
    const auto one = lattice.integer("1");
    const auto two = lattice.integer("2");
    prints(joined(lattice, {lattice.tuple({ok, one}), lattice.tuple({ok, two})}), "{ok, 1..2}");
    prints(joined(lattice, {lattice.tuple({ok, one}), lattice.tuple({lattice.atom("error"), lattice.atom("bad")})}),
           "{error, bad} | {ok, 1}");
    prints(joined(lattice, {lattice.tuple({one}), lattice.tuple({one, two})}), "{1} | {1, 2}");
    prints(joined(lattice, {lattice.nil(), lattice.list(one, true)}), "[1]");
    prints(joined(lattice, {lattice.list(one, true), lattice.list(two, true)}), "[1..2, ...]");
    prints(joined(lattice, {lattice.list(lattice.range({"0", "1114111"}), true)}), "nonempty_string()");
    prints(joined(lattice, {lattice.list(graph.top(), false)}), "list()");
    const auto a = lattice.atom("a");
    prints(joined(lattice, {lattice.map({a, one}), lattice.map({a, two})}), "#{a := 1..2}");
    prints(joined(lattice, {lattice.map({a, one}), lattice.map({ok, one})}), "#{a | ok => 1}");
    prints(joined(lattice, {lattice.map({a, one}), lattice.category("map")}), "map()");
    const auto pair = [&](t::Id first, t::Id second) { return lattice.positional({first, second, lattice.nil()}); };
    prints(joined(lattice, {pair(one, a), pair(two, ok)}), "[1..2, (a | ok)]");
    prints(joined(lattice, {pair(one, two), lattice.list(a, true)}), "[1..2 | a, ...]");
    prints(t::type_source(graph, lattice.positional({one, a, lattice.list(two, false)})), "[1, a | [2]]");
    prints(joined(lattice, {lattice.map({two, a, a, one})}), "#{2 := a, a := 1}");
    prints(joined(lattice, {lattice.fun(1, one), lattice.fun(1, two)}), "fun((_) -> 1..2)");
    prints(joined(lattice, {lattice.fun(0, one), lattice.fun(1, one)}), "fun()");
    prints(joined(lattice, {lattice.bitstring(16, 0), lattice.bitstring(16, 0)}), "<<_:16>>");
    prints(joined(lattice, {lattice.bitstring(8, 0), lattice.bitstring(16, 0)}), "nonempty_binary()");
    prints(joined(lattice, {lattice.bitstring(8, 0), lattice.bitstring(24, 0)}), "<<_:8, _:_*16>>");
    prints(joined(lattice, {lattice.bitstring(0, 0), lattice.bitstring(8, 0)}), "binary()");
    prints(joined(lattice, {lattice.bitstring(0, 0), lattice.bitstring(16, 0)}), "<<_:_*16>>");
    prints(joined(lattice, {lattice.bitstring(3, 0), lattice.bitstring(5, 0)}), "<<_:3, _:_*2>>");
}

// Unions past the member budget, containers past the depth and element budgets, become term(), tuple(), map().
void budgets() {
    t::Graph graph;
    t::Lattice lattice(graph, {.singletons = 8, .members = 3, .depth = 2, .elements = 2});
    const auto one = lattice.integer("1");
    prints(joined(lattice, {one, lattice.atom("a"), lattice.category("pid")}), "1 | a | pid()");
    prints(joined(lattice, {one, lattice.atom("a"), lattice.category("pid"), lattice.nil()}), "_");
    prints(t::type_source(graph, lattice.tuple({one, one, one})), "{1, 1, 1}");
    prints(t::type_source(graph, lattice.tuple({one, graph.top(), one})), "tuple()");
    prints(t::type_source(graph, lattice.map({one, one, lattice.integer("2"), one, lattice.integer("3"), one})),
           "#{1..3 => 1}");
    prints(t::type_source(graph, lattice.tuple({lattice.tuple({lattice.tuple({one})})})), "{{_}}");
    std::vector<t::Id> shapes;
    for (const auto *tag : {"a", "b", "c", "d"}) {
        shapes.push_back(lattice.tuple({lattice.atom(tag)}));
    }
    prints(joined(lattice, {shapes[0], shapes[1], shapes[2], shapes[3]}), "tuple()");
}

// Widening sends integer bounds that moved since the previous round to their category; unchanged facts stay.
void widening() {
    t::Graph graph;
    t::Lattice lattice(graph);
    const auto i = [&](int value) { return lattice.integer(std::to_string(value)); };
    const auto widened = [&](t::Id previous, t::Id next) {
        return t::type_source(graph, lattice.widen(previous, next));
    };
    prints(widened(graph.bottom(), lattice.join(i(1), i(2))), "1..2");
    prints(widened(lattice.join(i(1), i(2)), i(2)), "1..2");
    prints(widened(i(1), i(2)), "pos_integer()");
    prints(widened(i(0), i(1)), "non_neg_integer()");
    prints(widened(i(-1), i(-2)), "neg_integer()");
    prints(widened(i(0), i(-1)), "integer()");
    prints(widened(i(3), i(2)), "1..3");
    prints(widened(lattice.range({"-5", "-1"}), i(-9)), "neg_integer()");
    prints(widened(i(5), lattice.atom("done")), "5 | done");
}

// Narrowing keeps the values both facts hold: none() only when they share none; categories meet their members.
void meets() {
    t::Graph graph;
    t::Lattice lattice(graph);
    const auto i = [&](int value) { return lattice.integer(std::to_string(value)); };
    const auto met = [&](t::Id left, t::Id right) { return t::type_source(graph, lattice.meet(left, right)); };
    const auto category = [&](const char *name) { return lattice.category(name); };
    prints(met(graph.top(), i(1)), "1");
    prints(met(lattice.range({"1", "10"}), lattice.range({"5", "20"})), "5..10");
    prints(met(category("integer"), category("pos_integer")), "pos_integer()");
    prints(met(category("number"), category("float")), "float()");
    prints(met(lattice.join(i(1), category("float")), category("integer")), "1");
    prints(met(category("integer"), category("atom")), "none()");
    prints(met(category("boolean"), lattice.join(lattice.atom("true"), lattice.atom("ok"))), "true");
    prints(met(category("atom"), lattice.atom("ok")), "ok");
    prints(met(category("tuple"), lattice.tuple({i(1), i(2)})), "{1, 2}");
    prints(met(lattice.tuple({graph.top(), i(2)}), lattice.tuple({i(1), category("integer")})), "{1, 2}");
    prints(met(lattice.tuple({i(1)}), lattice.tuple({i(1), i(2)})), "none()");
    prints(met(category("map"), lattice.map({lattice.atom("a"), i(1)})), "#{a := 1}");
    prints(met(category("maybe_improper_list"), lattice.nil()), "[]");
    prints(met(category("list"), lattice.list(i(1), true)), "[1, ...]");
    prints(met(lattice.nil(), lattice.list(i(1), true)), "none()");
    prints(met(category("fun"), lattice.fun(2, i(1))), "fun((_, _) -> 1)");
    prints(met(lattice.fun(1, graph.top()), lattice.fun(2, graph.top())), "none()");
    prints(met(category("binary"), lattice.bitstring(16, 0)), "<<_:16>>");
    const auto pair = lattice.positional({i(1), lattice.atom("a"), lattice.nil()});
    prints(met(pair, lattice.list(category("integer"), false)), "none()");
    prints(met(pair, lattice.list(lattice.join(i(1), lattice.atom("a")), true)), "[1, a]");
    prints(met(pair, lattice.nil()), "none()");
    const auto association = lattice.association(lattice.range({"1", "3"}), lattice.atom("x"));
    prints(met(association, lattice.map({i(2), lattice.atom("x")})), "#{2 := x}");
    prints(met(association, lattice.map({i(5), lattice.atom("x")})), "none()");
    prints(met(category("pid"), category("port")), "none()");
    prints(t::type_source(graph, lattice.subtract(category("number"), category("integer"))), "float()");
    prints(t::type_source(graph, lattice.subtract(lattice.join(i(1), lattice.atom("a")), category("integer"))), "a");
}

// Joins of tree facts at the container depth limit hold both facts, so the rounds of a tree recursion only grow and
// settle (step 58S: they alternated between two unions, as ttdict:map/2 did).
void deep_tree_joins() {
    t::Graph graph;
    t::Lattice lattice(graph);
    const auto empty = lattice.atom("empty");
    const auto key = lattice.range({"1", "3"});
    auto previous = empty;
    std::vector<t::Id> seen;
    for (int round = 0; round < 40; ++round) {
        const auto two = lattice.tuple({previous, key, key, previous});
        const auto three = lattice.tuple({previous, key, key, previous, key, key, previous});
        const auto evaluated = lattice.join(lattice.join(empty, two), three);
        const auto next = lattice.join(previous, evaluated);
        require(lattice.within(previous, next) && lattice.within(evaluated, next));
        if (next == previous) {
            return;
        }
        require(!std::ranges::contains(seen, next));
        seen.push_back(previous);
        previous = next;
    }
    require(false);
}

int main() {
    try {
        local_inference();
        integer_joins();
        atom_joins();
        container_joins();
        budgets();
        deep_tree_joins();
        widening();
        meets();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("unexpected inference failure\n", stderr);
        return 1;
    }
}
