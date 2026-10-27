#include "syntax_lib.hpp"

// erl_syntax_lib:annotate_bindings/1,2 as builtins (plan 21 LIB77E4a): every node gets {env, Env}, {bound, Bound}
// and {free, Free} annotations, ordsets of variable names, by OTP's scoping rules for each expression and pattern.
namespace clause::runtime::builtins::syntax_lib {
namespace {
// An annotated node with the variables it binds and the ones it uses free.
struct Bound {
    Term tree;
    Names bound;
    Names free;
};

// Annotated nodes of a list with their joined bindings.
struct Many {
    Items trees;
    Names bound;
    Names free;
};

// The annotation walk over one process.
class Binder final {
  public:
    explicit Binder(ProcessContext &context) : context_(context), trees_(context) {}

    ProcessContext &context() const { return context_; }

    const Trees &trees() const { return trees_; }

    // vann/2 and vann_pattern/2.
    Bound expr(const Term &tree, const Names &env);
    Bound pattern(const Term &tree, const Names &env);
    // vann_clause/2 and vann_clauses/2: clauses keep the bindings every clause makes.
    Bound clause(const Term &tree, const Names &env);
    Many clauses(const Items &clauses, const Names &env);
    // vann_body/2 and vann_comp_body/2: each expression sees the bindings before it.
    Many body(const Term &list, const Names &env);
    Many comp_body(const Term &list, const Names &env);
    // vann_list/2 and vann_patterns/2: independent expressions or patterns.
    Many exprs(const Term &list, const Names &env);
    Many patterns(const Term &list, const Names &env);

    // ann_bindings/4: the node with fresh env, bound and free annotations before its other annotations.
    Bound annotated(const Term &tree, const Names &env, const Names &bound, const Names &free) const;

    // rewrite/2: a rebuilt node with the attributes of the original.
    Term rewrite(const Term &tree, const Term &made) const { return copy_attrs(context_, tree, made); }

    Term field(const Term &tree, FieldName name) const { return get(context_, tree, name); }

  private:
    // Every node is annotated in the walk's process.
    ProcessContext &context_;
    // Builds the annotation tuples and lists.
    Trees trees_;
};

using Handler = Bound (*)(Binder &binder, const Term &tree, const Names &env);

// Whether an annotation is one of {env, _}, {bound, _} or {free, _}.
bool is_binding_ann(const Term &ann) {
    if (!ann.is_tuple() || ann.tuple_size_unchecked() != 2) {
        return false;
    }
    const auto key = element(ann, 0);
    return is_atom(key, "env") || is_atom(key, "bound") || is_atom(key, "free");
}

Bound Binder::annotated(const Term &tree, const Names &env, const Names &bound, const Names &free) const {
    const auto pair = [&](AtomName key, const Names &names) {
        return trees_.tuple(std::array{trees_.atom(key), names.list(trees_)});
    };
    Items anns{pair("env"_atom, env), pair("bound"_atom, bound), pair("free"_atom, free)};
    for (const auto &ann : syntax::elements(get_ann(context_, tree))) {
        if (!is_binding_ann(ann)) {
            anns.push_back(ann);
        }
    }
    return {set_ann(context_, tree, {trees_.list(anns)}), bound, free};
}

Bound variable_use(Binder &binder, const Term &tree, const Names &env) {
    return binder.annotated(tree, env, {}, Names::of(binder.field(tree, "variable_name")));
}

// The default of vann/2: every subtree an expression, their bindings joined.
Bound subtree_exprs(Binder &binder, const Term &tree, const Names &env) {
    Names bound;
    Names free;
    const auto mapped = map_subtrees(binder.context(), tree, [&](const Term &item) {
        auto annotated = binder.expr(item, env);
        bound = bound.unite(annotated.bound);
        free = free.unite(annotated.free);
        return annotated.tree;
    });
    return binder.annotated(mapped, env, bound, free);
}

// A match: the body first, its bindings visible to the pattern (also maybe's ?= match).
template <FieldName PATTERN, FieldName BODY, SimpleName MAKE>
Bound match(Binder &binder, const Term &tree, const Names &env) {
    const auto body = binder.expr(binder.field(tree, BODY), env);
    const auto pattern = binder.pattern(binder.field(tree, PATTERN), env.unite(body.bound));
    const auto made = make(binder.context(), MAKE, std::array{pattern.tree, body.tree});
    return binder.annotated(binder.rewrite(tree, made), env, body.bound.unite(pattern.bound),
                            body.free.unite(pattern.free));
}

Bound else_expr(Binder &binder, const Term &tree, const Names &env) {
    const auto clauses = binder.clauses(syntax::elements(binder.field(tree, "else_expr_clauses")), env);
    const auto made = make(binder.context(), "else_expr", std::array{binder.trees().list(clauses.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, {}, clauses.free);
}

Bound maybe_expr(Binder &binder, const Term &tree, const Names &env) {
    auto &context = binder.context();
    const auto body = binder.body(maybe_expr_body(context, tree), env);
    const auto otherwise = maybe_expr_else(context, tree);
    const auto none = is_none(otherwise);
    const auto annotated_else = none ? Bound{otherwise, {}, {}} : else_expr(binder, otherwise, env);
    const auto made = make(context, "maybe_expr", std::array{binder.trees().list(body.trees), annotated_else.tree});
    return binder.annotated(binder.rewrite(tree, made), env, {}, body.free.unite(annotated_else.free));
}

Bound case_expr(Binder &binder, const Term &tree, const Names &env) {
    const auto argument = binder.expr(binder.field(tree, "case_expr_argument"), env);
    const auto clauses =
        binder.clauses(syntax::elements(binder.field(tree, "case_expr_clauses")), env.unite(argument.bound));
    const auto made =
        make(binder.context(), "case_expr", std::array{argument.tree, binder.trees().list(clauses.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, argument.bound.unite(clauses.bound),
                            argument.free.unite(clauses.free));
}

Bound if_expr(Binder &binder, const Term &tree, const Names &env) {
    const auto clauses = binder.clauses(syntax::elements(binder.field(tree, "if_expr_clauses")), env);
    const auto made = make(binder.context(), "if_expr", std::array{binder.trees().list(clauses.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, clauses.bound, clauses.free);
}

Bound catch_expr(Binder &binder, const Term &tree, const Names &env) {
    const auto body = binder.expr(binder.field(tree, "catch_expr_body"), env);
    const auto made = make(binder.context(), "catch_expr", std::array{body.tree});
    return binder.annotated(binder.rewrite(tree, made), env, {}, body.free);
}

// try: the body's bindings reach the success clauses only; nothing is exported.
Bound try_expr(Binder &binder, const Term &tree, const Names &env) {
    auto &context = binder.context();
    const auto body = binder.body(binder.field(tree, "try_expr_body"), env);
    const auto clauses =
        binder.clauses(syntax::elements(binder.field(tree, "try_expr_clauses")), env.unite(body.bound));
    const auto handlers = binder.clauses(syntax::elements(try_expr_handlers(context, tree)), env);
    const auto after = binder.body(binder.field(tree, "try_expr_after"), env);
    const auto &trees = binder.trees();
    const auto made = make(context, "try_expr",
                           std::array{trees.list(body.trees), trees.list(clauses.trees), trees.list(handlers.trees),
                                      trees.list(after.trees)});
    const auto free = body.free.unite(clauses.free.unite(handlers.free.unite(after.free)));
    return binder.annotated(binder.rewrite(tree, made), env, {}, free);
}

// receive: the timeout action is one more clause; bindings in the timeout expression stay local.
Bound receive(Binder &binder, const Term &tree, const Names &env) {
    auto &context = binder.context();
    const auto &trees = binder.trees();
    const auto action =
        clause(context, std::array{trees.nil(), trees.atom("none"_atom), receive_expr_action(context, tree)});
    auto clauses = syntax::elements(receive_expr_clauses(context, tree));
    clauses.insert(clauses.begin(), action);
    auto annotated = binder.clauses(clauses, env);
    const auto body = binder.field(annotated.trees.front(), "clause_body");
    annotated.trees.erase(annotated.trees.begin());
    const auto timeout = receive_expr_timeout(context, tree);
    const auto expiry = is_none(timeout) ? Bound{timeout, {}, {}} : binder.expr(timeout, env);
    const auto made = receive_expr(context, std::array{trees.list(annotated.trees), expiry.tree, body});
    return binder.annotated(binder.rewrite(tree, made), env, annotated.bound, annotated.free.unite(expiry.free));
}

Bound function(Binder &binder, const Term &tree, const Names &env) {
    auto &context = binder.context();
    const auto clauses = binder.clauses(syntax::elements(binder.field(tree, "function_clauses")), env);
    const auto name = binder.expr(function_name(context, tree), env);
    const auto made = make(context, "function", std::array{name.tree, binder.trees().list(clauses.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, {}, clauses.free);
}

Bound fun_expr(Binder &binder, const Term &tree, const Names &env) {
    const auto clauses = binder.clauses(syntax::elements(fun_expr_clauses(binder.context(), tree)), env);
    const auto made = make(binder.context(), "fun_expr", std::array{binder.trees().list(clauses.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, {}, clauses.free);
}

// A named fun binds its name for its own clauses.
Bound named_fun_expr(Binder &binder, const Term &tree, const Names &env) {
    auto &context = binder.context();
    const auto name = named_fun_expr_name(context, tree);
    const auto own = Names::of(binder.field(name, "variable_name"));
    const auto annotated_name = binder.annotated(name, env, own, {});
    const auto clauses = binder.clauses(syntax::elements(binder.field(tree, "named_fun_expr_clauses")), env.unite(own));
    const auto made =
        make(context, "named_fun_expr", std::array{annotated_name.tree, binder.trees().list(clauses.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, {}, clauses.free);
}

// A comprehension: the qualifiers' bindings reach the template; nothing is exported.
template <FieldName TEMPLATE, FieldName BODY, SimpleName MAKE>
Bound comprehension(Binder &binder, const Term &tree, const Names &env) {
    const auto body = binder.comp_body(binder.field(tree, BODY), env);
    const auto result = binder.expr(binder.field(tree, TEMPLATE), env.unite(body.bound));
    const auto made = make(binder.context(), MAKE, std::array{result.tree, binder.trees().list(body.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, {}, body.free.unite(result.free.minus(body.bound)));
}

// A generator binds its pattern's variables, matched afresh (OUTER: binary generators see the bindings around them).
template <FieldName PATTERN, FieldName BODY, SimpleName MAKE, bool OUTER>
Bound generator(Binder &binder, const Term &tree, const Names &env) {
    const auto pattern = binder.pattern(binder.field(tree, PATTERN), OUTER ? env : Names{});
    const auto body = binder.expr(binder.field(tree, BODY), env);
    const auto made = make(binder.context(), MAKE, std::array{pattern.tree, body.tree});
    return binder.annotated(binder.rewrite(tree, made), env, pattern.bound, body.free);
}

Bound zip_generator(Binder &binder, const Term &tree, const Names &env) {
    const auto body = binder.comp_body(binder.field(tree, "zip_generator_body"), env);
    const auto made = make(binder.context(), "zip_generator", std::array{binder.trees().list(body.trees)});
    return binder.annotated(binder.rewrite(tree, made), env.unite(body.bound), body.bound, body.free);
}

Bound block_expr(Binder &binder, const Term &tree, const Names &env) {
    const auto body = binder.body(binder.field(tree, "block_expr_body"), env);
    const auto made = make(binder.context(), "block_expr", std::array{binder.trees().list(body.trees)});
    return binder.annotated(binder.rewrite(tree, made), env, body.bound, body.free);
}

// A macro's name is not a variable; its arguments are expressions (PATTERN: patterns).
template <bool PATTERN> Bound macro(Binder &binder, const Term &tree, const Names &env) {
    const auto arguments = binder.field(tree, "macro_arguments");
    Many annotated;
    if (!is_none(arguments)) {
        annotated = PATTERN ? binder.patterns(arguments, env) : binder.exprs(arguments, env);
    }
    const auto annotated_arguments = is_none(arguments) ? arguments : binder.trees().list(annotated.trees);
    const auto made =
        make(binder.context(), "macro", std::array{binder.field(tree, "macro_name"), annotated_arguments});
    return binder.annotated(binder.rewrite(tree, made), env, annotated.bound, annotated.free);
}

struct Rule {
    std::string_view type;
    Handler handler;
};

constexpr auto GENERATORS = std::to_array<Rule>({
    {"generator", generator<"generator_pattern", "generator_body", "generator", false>},
    {"strict_generator", generator<"strict_generator_pattern", "strict_generator_body", "strict_generator", false>},
    {"binary_generator", generator<"binary_generator_pattern", "binary_generator_body", "binary_generator", true>},
    {"strict_binary_generator",
     generator<"strict_binary_generator_pattern", "strict_binary_generator_body", "strict_binary_generator", true>},
    {"map_generator", generator<"map_generator_pattern", "map_generator_body", "map_generator", false>},
    {"strict_map_generator",
     generator<"strict_map_generator_pattern", "strict_map_generator_body", "strict_map_generator", false>},
    {"zip_generator", zip_generator},
});

constexpr auto EXPRESSIONS = std::to_array<Rule>({
    {"variable", variable_use},
    {"match_expr", match<"match_expr_pattern", "match_expr_body", "match_expr">},
    {"maybe_expr", maybe_expr},
    {"maybe_match_expr", match<"maybe_match_expr_pattern", "maybe_match_expr_body", "maybe_match_expr">},
    {"case_expr", case_expr},
    {"else_expr", else_expr},
    {"if_expr", if_expr},
    {"receive_expr", receive},
    {"catch_expr", catch_expr},
    {"try_expr", try_expr},
    {"function", function},
    {"fun_expr", fun_expr},
    {"named_fun_expr", named_fun_expr},
    {"list_comp", comprehension<"list_comp_template", "list_comp_body", "list_comp">},
    {"binary_comp", comprehension<"binary_comp_template", "binary_comp_body", "binary_comp">},
    {"map_comp", comprehension<"map_comp_template", "map_comp_body", "map_comp">},
    {"block_expr", block_expr},
    {"macro", macro<false>},
});

Handler handler_of(std::span<const Rule> rules, std::string_view type) {
    const auto found = std::ranges::find(rules, type, &Rule::type);
    return found == rules.end() ? nullptr : found->handler;
}

Bound Binder::expr(const Term &tree, const Names &env) {
    const auto type = type_of(context_, tree);
    auto handler = handler_of(EXPRESSIONS, type);
    handler = handler != nullptr ? handler : handler_of(GENERATORS, type);
    return handler != nullptr ? handler(*this, tree, env) : subtree_exprs(*this, tree, env);
}

// A variable in a pattern is a use when the environment has it, else a binding.
Bound pattern_variable(Binder &binder, const Term &tree, const Names &env) {
    const auto name = Names::of(binder.field(tree, "variable_name"));
    if (env.contains(binder.field(tree, "variable_name"))) {
        return binder.annotated(tree, env, {}, name);
    }
    return binder.annotated(tree, env, name, {});
}

// An alias pattern: both sides are patterns.
template <FieldName LEFT, FieldName RIGHT, SimpleName MAKE>
Bound alias(Binder &binder, const Term &tree, const Names &env) {
    const auto left = binder.pattern(binder.field(tree, LEFT), env);
    const auto right = binder.pattern(binder.field(tree, RIGHT), env);
    const auto made = make(binder.context(), MAKE, std::array{left.tree, right.tree});
    return binder.annotated(binder.rewrite(tree, made), env, left.bound.unite(right.bound),
                            left.free.unite(right.free));
}

// The default of vann_pattern/2: every subtree a pattern, their bindings joined.
Bound subtree_patterns(Binder &binder, const Term &tree, const Names &env) {
    Names bound;
    Names free;
    const auto mapped = map_subtrees(binder.context(), tree, [&](const Term &item) {
        auto annotated = binder.pattern(item, env);
        bound = bound.unite(annotated.bound);
        free = free.unite(annotated.free);
        return annotated.tree;
    });
    return binder.annotated(mapped, env, bound, free);
}

constexpr auto PATTERNS = std::to_array<Rule>({
    {"variable", pattern_variable},
    {"match_expr", alias<"match_expr_pattern", "match_expr_body", "match_expr">},
    {"maybe_match_expr", alias<"maybe_match_expr_pattern", "maybe_match_expr_body", "maybe_match_expr">},
    {"macro", macro<true>},
});

Bound Binder::pattern(const Term &tree, const Names &env) {
    const auto handler = handler_of(PATTERNS, type_of(context_, tree));
    return handler != nullptr ? handler(*this, tree, env) : subtree_patterns(*this, tree, env);
}

Bound Binder::clause(const Term &tree, const Names &env) {
    const auto patterns_bound = patterns(field(tree, "clause_patterns"), env);
    const auto inner = env.unite(patterns_bound.bound);
    const auto guard = clause_guard(context_, tree);
    const auto annotated_guard = is_none(guard) ? Bound{guard, {}, {}} : expr(guard, inner);
    const auto body_bound = body(field(tree, "clause_body"), inner);
    const auto made = syntax::clause(
        context_, std::array{trees_.list(patterns_bound.trees), annotated_guard.tree, trees_.list(body_bound.trees)});
    const auto free =
        patterns_bound.free.unite(annotated_guard.free.unite(body_bound.free).minus(patterns_bound.bound));
    return annotated(rewrite(tree, made), env, patterns_bound.bound.unite(body_bound.bound), free);
}

Many Binder::clauses(const Items &clauses, const Names &env) {
    Many joined;
    for (const auto &item : clauses) {
        auto annotated = clause(item, env);
        joined.bound = joined.trees.empty() ? annotated.bound : joined.bound.common(annotated.bound);
        joined.free = joined.free.unite(annotated.free);
        joined.trees.push_back(annotated.tree);
    }
    return joined;
}

Many Binder::body(const Term &list, const Names &env) {
    Many joined;
    auto scope = env;
    for (const auto &item : syntax::elements(list)) {
        auto annotated = expr(item, scope);
        scope = scope.unite(annotated.bound);
        joined.free = joined.free.unite(annotated.free.minus(joined.bound));
        joined.bound = joined.bound.unite(annotated.bound);
        joined.trees.push_back(annotated.tree);
    }
    return joined;
}

Many Binder::comp_body(const Term &list, const Names &env) {
    Many joined;
    auto scope = env;
    for (const auto &item : syntax::elements(list)) {
        const auto handler = handler_of(GENERATORS, type_of(context_, item));
        auto annotated = handler != nullptr ? handler(*this, item, scope) : expr(item, scope);
        // Bindings in filters are not exported to the rest of the body.
        annotated.bound = handler != nullptr ? annotated.bound : Names{};
        scope = scope.unite(annotated.bound);
        joined.free = joined.free.unite(annotated.free.minus(joined.bound));
        joined.bound = joined.bound.unite(annotated.bound);
        joined.trees.push_back(annotated.tree);
    }
    return joined;
}

Many Binder::exprs(const Term &list, const Names &env) {
    Many joined;
    for (const auto &item : syntax::elements(list)) {
        auto annotated = expr(item, env);
        joined.bound = joined.bound.unite(annotated.bound);
        joined.free = joined.free.unite(annotated.free);
        joined.trees.push_back(annotated.tree);
    }
    return joined;
}

Many Binder::patterns(const Term &list, const Names &env) {
    Many joined;
    for (const auto &item : syntax::elements(list)) {
        auto annotated = pattern(item, env);
        joined.bound = joined.bound.unite(annotated.bound);
        joined.free = joined.free.unite(annotated.free);
        joined.trees.push_back(annotated.tree);
    }
    return joined;
}

Term annotate_bindings2(ProcessContext &context, const Term &tree, const Term &env) {
    Binder binder(context);
    return binder.expr(tree, Names(env)).tree;
}

// annotate_bindings/1: the environment comes from the node's own {env, Env} annotation; badarg without one.
Term annotate_bindings1(ProcessContext &context, const Term &tree) {
    for (const auto &ann : syntax::elements(get_ann(context, tree))) {
        if (ann.is_tuple() && ann.tuple_size_unchecked() > 0 && is_atom(element(ann, 0), "env")) {
            if (ann.tuple_size_unchecked() != 2) {
                bad_argument();
            }
            return annotate_bindings2(context, tree, element(ann, 1));
        }
    }
    bad_argument();
}

constexpr std::array BINDINGS{
    typed_entry<annotate_bindings1>("erl_syntax_lib", "annotate_bindings"),
    typed_entry<annotate_bindings2>("erl_syntax_lib", "annotate_bindings"),
};
} // namespace
} // namespace clause::runtime::builtins::syntax_lib

namespace clause::runtime {
// Publish erl_syntax_lib's binding annotations.
std::span<const BuiltinEntry> erl_syntax_lib_binding_builtins() noexcept { return builtins::syntax_lib::BINDINGS; }
} // namespace clause::runtime
