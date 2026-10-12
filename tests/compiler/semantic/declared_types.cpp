#include "semantic/types/declarations.hpp"
#include <array>
#include <clause/compiler/parser.hpp>
#include <cstdio>
#include <source_location>
#include <stdexcept>
using namespace clause;
using namespace clause::semantic;
namespace t = clause::semantic::types;

// Inspect opacity and substitution invariants until semantic type output exists in step39.
void require(bool value, const std::source_location site = std::source_location::current()) {
    if (!value) {
        throw std::runtime_error("declared type invariant at line " + std::to_string(site.line()));
    }
}

// Exercise substitution, hidden structure, memoization and budget fallback through actual declarations.
void declared_types() {
    SourceManager sources;
    PreprocessorSession pp(sources.add("owner.erl", R"(
-module(owner).
-export_type([box/1,secret/0,distinct/0,tree/1]).
-type box(T) :: {T,T}.
-opaque secret() :: integer().
-nominal distinct() :: integer().
-type tree(T) :: nil | {T,tree(T)}.
-type last(A,A) :: A.
-spec id(A) -> A when A :: integer().
id(X) -> X.
)"));
    auto parsed = parse_module(pp);
    require(!parsed.failed);
    bool failed = false;
    const Reporter report = [&](const Diagnostic &d) { failed = failed || d.severity == Severity::error; };
    std::vector<std::unique_ptr<Module>> modules;
    modules.push_back(index(parsed.module, "owner.erl", report));
    auto registry = t::resolve_declarations(modules, report);
    require(!failed);
    auto &graph = registry->graph;
    const auto integer = graph.intern({t::Kind::integer, "42"});
    const auto box = graph.intern({t::Kind::reference, "box", "owner", {integer}});
    const auto expanded = t::expand_reference(*registry, box, "outside");
    require(expanded.has_value());
    require(graph.get(*expanded).kind == t::Kind::tuple &&
            graph.get(*expanded).children == std::vector{integer, integer});
    const auto secret = graph.intern({t::Kind::reference, "secret", "owner"});
    require(!t::expand_reference(*registry, secret, "outside"));
    require(t::expand_reference(*registry, secret, "owner").has_value());
    require(!t::expand_reference(*registry, secret, "outside"));
    const auto nominal = graph.intern({t::Kind::reference, "distinct", "owner"});
    require(nominal != secret && nominal != integer);
    require(!t::expand_reference(*registry, nominal, "outside"));
    require(t::expand_reference(*registry, nominal, "owner").has_value());
    const auto tree = graph.intern({t::Kind::reference, "tree", "owner", {integer}});
    require(t::expand_reference(*registry, tree, "outside").has_value() && !graph.widened());
    require(registry->contracts.size() == 1 && registry->contracts[0].overloads[0].constraints.size() == 1);
    require(t::expand_reference(*registry, box, "outside") == expanded);
    const auto other = graph.intern({t::Kind::atom, "other"});
    const auto repeated = graph.intern({t::Kind::reference, "last", "owner", {integer, other}});
    require(t::expand_reference(*registry, repeated, "owner") == other);
    const auto &function = graph.get(registry->contracts[0].overloads[0].function);
    require(function.children[0] == function.children[1]);
    auto bounded = t::resolve_declarations(modules, report, {.nodes = 2});
    require(!failed && bounded->graph.widened());
    auto work_limited = t::resolve_declarations(modules, report, {.type_work = 1});
    require(!failed && work_limited->graph.widened());
}

// Quoted delimiters in names must not alias quantifiers in another module's declaration.
void scope_identity() {
    SourceManager sources;
    PreprocessorSession first(sources.add("first.erl", "-module('a:type:b'). -type c(T) :: {T,T}."));
    PreprocessorSession second(sources.add("second.erl", "-module(a). -type 'b:type:c'(T) :: {T,T}."));
    auto left = parse_module(first);
    auto right = parse_module(second);
    require(!left.failed && !right.failed);
    const Reporter report = [](const Diagnostic &d) { require(d.severity != Severity::error); };
    std::vector<std::unique_ptr<Module>> modules;
    modules.push_back(index(left.module, "first.erl", report));
    modules.push_back(index(right.module, "second.erl", report));
    const auto registry = t::resolve_declarations(modules, report);
    require(registry->declarations[0].body != registry->declarations[1].body);
}

int main() {
    try {
        declared_types();
        scope_identity();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("unexpected declared type failure\n", stderr);
        return 1;
    }
}
