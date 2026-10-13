#include "semantic/bindings.hpp"
#include "semantic/services.hpp"
#include <algorithm>
#include <clause/compiler/parser.hpp>
#include <cstdio>
#include <source_location>
#include <stdexcept>

using namespace clause;
using namespace clause::semantic;

// Clause identities and tentative rollback cannot be observed through execution until step 9.
void require(const bool value, const std::source_location site = std::source_location::current()) {
    if (!value) {
        throw std::runtime_error("binding invariant at line " + std::to_string(site.line()));
    }
}

// Analyze real syntax independently of the still-closed matching/guard execution capability gates.
void inspect(const std::string &source, const std::function<void(Module &, const std::vector<Diagnostic> &)> &check,
             const std::optional<std::size_t> budget = {}) {
    SourceManager sources;
    PreprocessorSession pp(sources.add("bindings.erl", source));
    auto parsed = parse_module(pp);
    require(!parsed.failed);
    std::vector<Diagnostic> diagnostics;
    const Reporter report = [&](const Diagnostic &d) { diagnostics.push_back(d); };
    auto module = index(parsed.module, "bindings.erl", report);
    bind_parameters(*module, report, budget);
    check(*module, diagnostics);
}

// Definitions are clause-local, wildcards are absent and repeated names preserve exact-equality obligations.
void identities() {
    inspect(R"(
-module(bindings).
f(X, X, _) when is_integer(X) -> X;
f(_, X, _) -> X.
project({X}, Y) -> X.
alias(A = B) -> B.
)",
            [](Module &module, const std::vector<Diagnostic> &errors) {
                require(errors.empty());
                const auto &f = module.functions[0];
                require(f.clause_bindings.size() == 2);
                require(f.clause_bindings[0].definitions.size() == 1);
                require(f.clause_bindings[1].definitions.size() == 1);
                require(f.bindings.size() == 6);
                require(f.bindings[0].use == BindingUse::definition);
                require(f.bindings[1].use == BindingUse::exact_check);
                require(f.bindings[0].identity == f.bindings[1].identity);
                require(f.bindings[2].context == BindingContext::guard);
                require(f.bindings[2].identity == f.bindings[3].identity);
                require(f.bindings[4].identity != f.bindings[0].identity);
                require(binding_argument(f, f.bindings[3].expression) == 0);
                require(binding_argument(f, f.bindings[5].expression) == 1);
                require(!binding_argument(f, f.bindings[1].expression));
                const auto &project = module.functions[1];
                require(!binding_argument(project, project.bindings.back().expression));
                const auto &alias = module.functions[2];
                require(binding_argument(alias, alias.bindings.back().expression) == 0);
                const auto original = f.bindings;
                bind_parameters(module, [](const Diagnostic &) { require(false); });
                require(original.size() == module.functions[0].bindings.size());
                for (std::size_t i = 0; i < original.size(); ++i) {
                    require(original[i].identity == module.functions[0].bindings[i].identity);
                }
            });
}

// Chained matches define RHS names first; sibling matches share checks without granting sibling reads.
void body_scopes() {
    inspect(R"(
-module(bindings).
chain(X) -> Z = Y = X, Y = Z, Z.
siblings() -> {X = 4, _ = X = 3}, X.
)",
            [](Module &module, const std::vector<Diagnostic> &errors) {
                require(errors.empty());
                const auto &chain = module.functions[0];
                const auto &definitions = chain.clause_bindings[0].definitions;
                require(definitions.size() == 3);
                require(definitions[1].name == U"Y" && definitions[2].name == U"Z");
                require(!definitions[1].argument && !definitions[2].argument);
                require(std::ranges::count(chain.bindings, BindingUse::exact_check, &Binding::use) == 1);
                require(!binding_argument(chain, chain.bindings.back().expression));
                const auto &siblings = module.functions[1];
                require(siblings.clause_bindings[0].definitions.size() == 1);
                require(std::ranges::count(siblings.bindings, BindingUse::exact_check, &Binding::use) == 1);
            });
}

// A rejected candidate cannot mutate its input; explicit success alone publishes tentative names.
void candidates() {
    BindingEnvironment incoming;
    incoming.names.emplace(U"X", BindingId{0, 0});
    {
        BindingCandidate failed{incoming, {}};
        failed.tentative.emplace(U"Y", BindingId{0, 1});
        require(failed.find(U"X") == BindingId{0, 0} && failed.find(U"Y") == BindingId{0, 1});
        failed.valid = false;
        failed.commit(incoming);
    }
    require(incoming.names.size() == 1 && !incoming.names.contains(U"Y"));
    BindingCandidate successful{incoming, {}};
    successful.tentative.emplace(U"Y", BindingId{0, 2});
    require(incoming.names.size() == 1);
    successful.commit(incoming);
    require(incoming.names.at(U"Y") == BindingId{0, 2});
}

// Small injected ceilings exercise exhaustion without relying on production-size source allocation.
void budgets() {
    inspect(
        "-module(bindings). f(X) -> g(g(X)). g(X) -> X.",
        [](Module &module, const std::vector<Diagnostic> &errors) {
            require(errors.size() == 1);
            require(errors[0].message == "binding analysis work limit exceeded");
            for (const auto &function : module.functions) {
                require(function.bindings.empty() && function.clause_bindings.empty());
            }
        },
        2);
}

// A large generated module (yecc-style state functions, each with a -compile attribute and a call of the
// auto-imported hd/1, like OTP's erl_parse) needs more than the budget floor. The default budgets grow with the
// module's expressions, and each name's auto-import state is scanned once, not once per call, so binding and
// service resolution succeed. Added for large generated modules.
void large_modules() {
    std::string source = "-module(bindings).\n";
    for (int state = 0; state < 8000; ++state) {
        const auto name = "yeccpars2_" + std::to_string(state);
        source += "-compile({nowarn_unused_function, " + name + "/6}).\n";
        source += name + "(S, Cat, Ss, Stack, T, Ts) -> yeccpars2_" + std::to_string((state + 1) % 8000) + "(hd(Ss), " +
                  std::to_string(state) + ", [S | Ss], [T | Stack], Cat, Ts).\n";
    }
    inspect(
        source,
        [](Module &, const std::vector<Diagnostic> &errors) {
            require(errors.size() == 1 && errors[0].message == "binding analysis work limit exceeded");
        },
        BINDING_WORK_FLOOR);
    inspect(source, [](Module &module, const std::vector<Diagnostic> &errors) {
        require(errors.empty() && !module.functions.back().bindings.empty());
        std::size_t failures = 0;
        resolve_services(module, [&](const Diagnostic &) { ++failures; });
        require(failures == 0 && !module.functions.back().services.empty());
    });
}

int main() {
    try {
        identities();
        body_scopes();
        candidates();
        budgets();
        large_modules();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    } catch (...) {
        std::fputs("unexpected binding failure\n", stderr);
        return 1;
    }
    std::puts("semantic_bindings: ok");
}
