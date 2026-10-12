#include "semantic/types/syntax.hpp"
#include <array>
#include <clause/compiler/parser.hpp>
#include <iostream>
#include <set>
#include <source_location>
#include <stdexcept>
using namespace clause;
using namespace clause::semantic::types;

// Locate the source declaration after preprocessing has inserted file provenance forms.
const ast::TypeDeclaration &first_type(const ast::Module &module) {
    for (const auto &id : module.forms()) {
        if (const auto *type = std::get_if<ast::TypeDeclaration>(&module.form(id).value)) {
            return *type;
        }
    }
    throw std::runtime_error("missing test type declaration");
}

// Internal type identities and widening have no public inspection action before plan step39.
void require(bool value, const std::source_location site = std::source_location::current()) {
    if (!value) {
        throw std::runtime_error("semantic type invariant at line " + std::to_string(site.line()));
    }
}

// Exercise ownership and lattice laws independently of future inferred function behavior.
void lattice() {
    Graph g({.nodes = 100, .union_members = 2});
    Graph other;
    const auto a = g.intern({Kind::integer, "1"});
    const auto b = g.intern({Kind::integer, "99999999999999999999999"});
    require(a == g.intern({Kind::integer, "1"}));
    require(a != b);
    require(g.join(std::array{a, g.bottom()}) == a);
    require(g.join(std::array{a, g.top()}) == g.top());
    const auto u = g.join(std::array{a, b});
    require(u == g.join(std::array{b, a, a}));
    require(g.widen(u, a) == u);
    const auto c = g.intern({Kind::atom, "ok"});
    require(g.widen(u, c) == g.top() && g.widened());
    bool foreign = false;
    try {
        (void)other.get(a);
    } catch (const std::invalid_argument &) {
        foreign = true;
    }
    require(foreign);
    Graph bounded({.nodes = 2});
    require(bounded.intern({Kind::atom, "x"}) == bounded.top() && bounded.widened());
    require(g.join(std::array{g.bottom(), g.bottom()}) == g.bottom());
    require(g.widen(g.top(), a) == g.top());
    Graph growth;
    const auto stable = growth.intern({Kind::atom, "stable"});
    for (std::size_t i = 0; i < 1000; ++i) {
        (void)growth.intern({Kind::integer, std::to_string(i)});
    }
    require(growth.get(stable).name == "stable");
    bool child_foreign = false;
    try {
        (void)other.intern({Kind::tuple, "exact", {}, {a}});
    } catch (const std::invalid_argument &) {
        child_foreign = true;
    }
    require(child_foreign);
}

// Parse real syntax for every type alternative; semantic traversal never mutates its owner.
void syntax_categories() {
    SourceManager sources;
    PreprocessorSession pp(sources.add("types.erl", R"(
-type a(T) :: {ok, T, 999999999999999999999999999999, $a, (integer()), X :: atom(), 1 | 2,
  1..10, -1, 1+2, remote:box(T), tuple(), [T,...], #{atom() := T}, #r{x :: T},
  <<_:8, _:_*16>>, fun((T) -> T)}.
)"));
    auto parsed = parse_module(pp);
    require(!parsed.failed);
    Graph graph;
    const auto &decl = first_type(parsed.module);
    const auto root = translate(graph, parsed.module, decl.type);
    require(graph.get(root).kind == Kind::tuple);
    std::set<Kind> kinds;
    std::vector<Id> pending{root};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto &node = graph.get(id);
        kinds.insert(node.kind);
        pending.insert(pending.end(), node.children.begin(), node.children.end());
    }
    for (const auto kind : {Kind::atom, Kind::variable, Kind::integer, Kind::annotation, Kind::union_type, Kind::range,
                            Kind::unary, Kind::binary, Kind::application, Kind::tuple, Kind::list, Kind::map,
                            Kind::record, Kind::bitstring, Kind::function}) {
        require(kinds.contains(kind));
    }
    Graph bounded({.type_work = 1});
    require(translate(bounded, parsed.module, decl.type) == bounded.top() && bounded.widened());
}

// Preserve distinctions between unrestricted containers, singleton empties and field/cardinality roles.
void structural_distinctions() {
    SourceManager sources;
    PreprocessorSession pp(sources.add("variants.erl", R"(
-type variants() :: {tuple(), {}, map(), #{}, [], [_], [_,...],
 fun(), fun((...) -> integer()), fun(() -> integer()),
 #{atom() => integer()}, #{atom() := integer()}, <<>>, <<_:8>>, <<_:_*8>>}.
)"));
    auto parsed = parse_module(pp);
    require(!parsed.failed);
    const auto &decl = first_type(parsed.module);
    Graph graph;
    const auto root = translate(graph, parsed.module, decl.type);
    const auto &children = graph.get(root).children;
    require(children.size() == 15);
    const std::set<Id> identities(children.begin(), children.end());
    require(identities.size() == children.size());
}

int main() {
    try {
        lattice();
        syntax_categories();
        structural_distinctions();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
