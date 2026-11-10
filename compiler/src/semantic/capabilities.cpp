#include "expression_capability.hpp"
#include "features.hpp"
#include "match_plan.hpp"
#include "services.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace clause::semantic {
namespace {
// Attributes that would change semantics (nifs, module parameters) stay rejected; -on_load runs before the entry
// since plan 21 step MIM9d.
struct FormCapability {
    std::string_view operator()(const ast::ModuleAttribute &value) const {
        return value.parameters ? "behavior-changing attributes" : "";
    }

    std::string_view operator()(const ast::FileAttribute &) const { return {}; }

    std::string_view operator()(const ast::Function &) const { return {}; }

    std::string_view operator()(const ast::ExportAttribute &) const { return {}; }

    std::string_view operator()(const ast::TypeDeclaration &) const { return {}; }

    std::string_view operator()(const ast::Specification &) const { return {}; }

    std::string_view operator()(const ast::DocumentationAttribute &) const { return {}; }

    std::string_view operator()(const ast::ImportAttribute &) const { return {}; }

    std::string_view operator()(const ast::ImportRecordAttribute &) const { return {}; }

    std::string_view operator()(const ast::RecordDeclaration &) const { return {}; }

    // Other attributes are data kept for module_info(attributes); -compile admits only inert options.
    std::string_view operator()(const ast::GenericAttribute &value) const {
        constexpr std::array<std::u32string_view, 2> changing{U"nifs", U"compile"};
        return std::ranges::contains(changing, value.name.name) ? "behavior-changing attributes" : "";
    }
};

// Use one located diagnostic path for all capability rejection sites.
void unsupported(const Module &module, const ast::NodeSource &source, const std::string_view reason,
                 const Reporter &out) {
    reject_capability(module, source, reason, out);
}

// Literal limits precede admission and lowering even when the enclosing expression could be folded.
bool literal_limit(const Module &module, const ast::Expression &expression, const Reporter &out) {
    const auto *map = std::get_if<ast::MapExpression>(&expression.value);
    if (map && !map->base &&
        std::ranges::any_of(map->fields, [](const auto &field) { return field.kind == ast::MapFieldKind::exact; })) {
        report(module, &expression.source, "map construction requires '=>' associations", out);
        return true;
    }
    return false;
}

// Resolve local capability and literal limits before scheduling an expression's children.
bool available(const Module &module, const ast::ExprId &id, const Reporter &out, const unsigned bits) {
    const auto &expression = module.syntax->expression(id);
    if (literal_limit(module, expression, out)) {
        return false;
    }
    const auto reason = std::visit(ExpressionCapability{*module.syntax, id, bits, module}, expression.value);
    if (!reason.empty()) {
        unsupported(module, expression.source, reason, out);
        return false;
    }
    return true;
}

// Plan the head of every clause of an anonymous fun.
void fun_patterns(const Module &module, const Function &function, const ast::ExprValue &value, const Reporter &out,
                  const unsigned bits) {
    for (const auto &clause : *fun_clauses(value)) {
        (void)make_match_plan(module, function, clause, out, {.word_bits = bits});
    }
}

// Plan the patterns of a maybe's ?= matches.
void maybe_patterns(const Module &module, const Function &function, const ast::MaybeExpression &block,
                    const Reporter &out, const unsigned bits) {
    for (const auto &item : block.body) {
        if (const auto *match = std::get_if<ast::MaybeMatch>(&item)) {
            (void)make_match_plan(module, function, pattern_root(*module.syntax, match->pattern), out,
                                  {.word_bits = bits});
        }
    }
}

// Plan every generator pattern of a comprehension.
void generator_patterns(const Module &module, const Function &function, const ast::ExprValue &value,
                        const Reporter &out, const unsigned bits) {
    for (const auto &qualifier : *comprehension_qualifiers(value)) {
        for (const auto &part : zipped(qualifier)) {
            for (const auto &pattern : semantic::generator_patterns(part)) {
                (void)make_match_plan(module, function, pattern_root(*module.syntax, pattern), out,
                                      {.word_bits = bits});
            }
        }
    }
}

// Plan the patterns of the scopes an expression opens: ?= matches, generators and anonymous fun heads.
void scope_patterns(const Module &module, const Function &function, const ast::Expression &expression,
                    const Reporter &out, const unsigned bits) {
    if (const auto *block = std::get_if<ast::MaybeExpression>(&expression.value)) {
        maybe_patterns(module, function, *block, out, bits);
    }
    if (comprehension_qualifiers(expression.value)) {
        generator_patterns(module, function, expression.value, out, bits);
    }
    if (fun_clauses(expression.value)) {
        fun_patterns(module, function, expression.value, out, bits);
    }
}

// Plan body-match and case-clause patterns so unsupported pattern forms are diagnosed before lowering.
// A failed binding pass discards every binding table, leaving nothing to plan.
void patterns(const Module &module, const Function &function, const ast::Expression &expression, const Reporter &out,
              const unsigned bits) {
    if (function.clause_bindings.empty()) {
        return;
    }
    if (const auto *match = std::get_if<ast::MatchExpression>(&expression.value)) {
        (void)make_match_plan(module, function, match->left, out, {.word_bits = bits});
    }
    scope_patterns(module, function, expression, out, bits);
    for (const auto &clause : branch_clauses(expression.value)) {
        const auto exception = clause.handler ? clause.handler->exception_class : std::nullopt;
        if (exception) {
            (void)make_match_plan(module, function, *exception, out, {.word_bits = bits});
        }
        if (clause.pattern) {
            (void)make_match_plan(module, function, pattern_root(*module.syntax, *clause.pattern), out,
                                  {.word_bits = bits});
        }
    }
}

// A top-level match qualifier needs the experimental compr_assign feature, whose execution is deferred.
void assignment(const Module &module, const ast::Qualifier &qualifier, const Reporter &out) {
    const auto *filter = std::get_if<ast::FilterQualifier>(&qualifier.value);
    if (!filter || !std::holds_alternative<ast::MatchExpression>(
                       module.syntax->expression(ungroup(*module.syntax, filter->expression)).value)) {
        return;
    }
    const auto features = module.syntax->features();
    if (features && std::ranges::contains(features->enabled, "compr_assign")) {
        unsupported(module, qualifier.source, "heap expressions", out);
        return;
    }
    report(module, &qualifier.source,
           "matches using '=' are not allowed in comprehension qualifiers unless the experimental 'compr_assign' "
           "language feature is enabled",
           out);
}

// Map comprehension templates associate with `=>` (OTP illegal_map_exact_in_comprehension).
void check_templates(const Module &module, const ast::ExprValue &value, const Reporter &out) {
    const auto *map = std::get_if<ast::MapComprehension>(&value);
    if (!map) {
        return;
    }
    for (const auto &field : map->templates) {
        if (field.kind == ast::MapFieldKind::exact) {
            report(module, &field.source, "illegal map association, did you mean to use `=>`?", out);
        }
    }
}

// Zip groups may only contain generators (OTP illegal_zip_generator).
void check_qualifiers(const Module &module, const ast::ExprValue &value, const Reporter &out) {
    const auto *qualifiers = comprehension_qualifiers(value);
    if (!qualifiers) {
        return;
    }
    check_templates(module, value, out);
    for (const auto &qualifier : *qualifiers) {
        const bool zip = std::holds_alternative<ast::ZippedQualifier>(qualifier);
        for (const auto &part : zipped(qualifier)) {
            if (zip && !generator_input(part)) {
                report(module, &part.source, "only generators are allowed in a zip generator", out);
            }
            assignment(module, part, out);
        }
    }
}

// Inspect every executable child iteratively, including unused functions and nested call arguments.
void expressions(const Module &module, const Function &function, std::vector<ast::ExprId> pending, const Reporter &out,
                 const unsigned bits) {
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto &expression = module.syntax->expression(id);
        if (!available(module, id, out, bits)) {
            continue;
        }
        if (integer_literal(*module.syntax, id, bits)) {
            continue;
        }
        patterns(module, function, expression, out, bits);
        check_qualifiers(module, expression.value, out);
        const auto children = expression_children(module, expression);
        pending.insert(pending.end(), children.rbegin(), children.rend());
    }
}

// Every ordered body expression gets located capability analysis before any publication.
void body(const Module &module, const Function &function, const ast::FunctionClause &clause, const Reporter &out,
          const unsigned bits) {
    expressions(module, function, clause.body, out, bits);
}

// Parameter patterns and guards remain separate from expression capability decisions.
void head(const Module &module, const Function &function, const std::size_t index, const ast::FunctionClause &clause,
          const Reporter &out, const unsigned bits) {
    if (clause.guard) {
        for (const auto &alternative : clause.guard->alternatives) {
            expressions(module, function, alternative.tests, out, bits);
        }
    }
    if (!function.clause_bindings.empty()) {
        (void)make_match_plan(module, function, out, {.clause = index, .word_bits = bits});
    }
}

// Validate every candidate, including unreachable or unexported bodies.
void function(const Module &module, const Function &function, const ast::Function &value, const Reporter &out,
              const unsigned bits) {
    expressions(module, function, pattern_reads(module, function), out, bits);
    for (std::size_t i = 0; i < value.clauses.size(); ++i) {
        const auto &clause = value.clauses[i];
        head(module, function, i, clause, out, bits);
        body(module, function, clause, out, bits);
    }
}
} // namespace

void check_capabilities(const Module &module, const Reporter &out, const unsigned word_bits) {
    for (const auto &id : module.syntax->forms()) {
        const auto &form = module.syntax->form(id);
        const auto reason = std::visit(FormCapability{}, form.value);
        const auto rejected = reason.empty() ? std::nullopt : rejected_attribute(*module.syntax, form.value);
        if (rejected) {
            reject_capability(module, form.source, reason, out, *rejected);
        }
        if (const auto *value = std::get_if<ast::Function>(&form.value)) {
            const auto key = FunctionKey{value->name.name, value->clauses.at(0).arguments.size()};
            function(module, module.functions.at(module.lookup.at(key)), *value, out, word_bits);
        }
    }
}
} // namespace clause::semantic
