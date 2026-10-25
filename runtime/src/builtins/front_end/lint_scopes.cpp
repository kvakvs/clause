#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>

// erl_lint's scopes (plan 21 step LIB34C): icrt_export/4 merging the variables of the clauses of if, case, receive
// and try, and the qualifiers and templates of comprehensions.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// One entry of a clause's table and the clause it comes from, for lists:merge/1's order.
struct Merged {
    VarEntry entry;
    std::size_t clause;
};

// A variable in scope before the clauses: an exported one keeps its export merged over the clauses, others are bound.
VarEntry reused_variable(std::span<const std::pair<Merged, Term>> group, const VarEntry &old) {
    VarEntry result{.name = old.name, .info = var_info(VarKind::bound, Term{}, true, old.info.annos)};
    if (group.front().first.entry.info.kind == VarKind::exported && old.info.kind == VarKind::exported) {
        result.info.kind = old.info.kind;
        result.info.where = old.info.where;
        for (const auto &[item, key] : group) {
            merge_state(result.info, item.entry.info);
        }
    }
    return result;
}

// A variable new in the clauses: exported from the construct when every clause binds it, else unsafe in it.
VarEntry new_variable(std::span<const std::pair<Merged, Term>> group, const Term &in, std::size_t clauses) {
    auto state = var_info(VarKind::exported, in, false, {});
    std::vector<Term> annos;
    for (const auto &[item, key] : group) {
        const auto &info = item.entry.info;
        if (info.kind == VarKind::unsafe) {
            state = state.kind == VarKind::unsafe ? var_info(VarKind::unsafe, in, false, {})
                                                  : var_info(info.kind, info.where, false, {});
        }
        if (!info.used) {
            annos = merge_annos(annos, info.annos);
        }
    }
    if (group.size() != clauses) {
        state = var_info(VarKind::unsafe, in, false, {});
    }
    return {.name = group.front().first.entry.name, .info = var_info(state.kind, state.where, annos.empty(), annos)};
}

using MergedEntries = std::vector<std::pair<Merged, Term>>;

// The entries of all clauses' tables ordered as lists:merge/1 does: by name and state.
MergedEntries merged_entries(const Lint &lint, const std::vector<VarTable> &tables) {
    MergedEntries merged;
    for (std::size_t clause = 0; clause < tables.size(); ++clause) {
        for (const auto &entry : tables[clause]) {
            merged.push_back({{entry, clause}, lint.pair(entry.name, lint.state_term(entry.info))});
        }
    }
    std::ranges::stable_sort(merged, [](const auto &left, const auto &right) {
        return collections::compare(left.second, right.second) < 0;
    });
    return merged;
}

// The end of the run of entries named like the one at `at`.
std::size_t group_end(const MergedEntries &merged, std::size_t at) {
    auto end = at;
    while (end < merged.size() && merged[end].first.entry.name.word() == merged[at].first.entry.name.word()) {
        ++end;
    }
    return end;
}

// The first entry of the scope's table not before `name`.
VarTable::const_iterator skip_older(VarTable::const_iterator old, VarTable::const_iterator end, const Term &name) {
    while (old != end && old->name.atom_spelling_unchecked() < name.atom_spelling_unchecked()) {
        ++old;
    }
    return old;
}

// Whether the qualifier is a generator: a list, bit string or map one, strict or not.
bool is_generator(const Term &qualifier) {
    return qualifier.is_tuple() && qualifier.tuple_size_unchecked() == 4 &&
           forms::is_one_of(forms::tag_of(qualifier), {"generate", "generate_strict", "b_generate", "b_generate_strict",
                                                       "m_generate", "m_generate_strict"});
}
} // namespace

// The variable state as erl_lint's term ({S, U, As}), for comparing entries as lists:merge/1 does.
Term Lint::state_term(const VarInfo &info) const {
    Term state = atom("bound"_atom);
    if (info.kind == VarKind::exported) {
        state = pair(atom("export"_atom), info.where);
    } else if (info.kind == VarKind::unsafe) {
        state = pair(atom("unsafe"_atom), info.where);
    } else if (info.kind == VarKind::stacktrace) {
        state = atom("stacktrace"_atom);
    }
    return triple(state, atom(info.used ? "used"_atom : "unused"_atom), build_.list(info.annos));
}

VarTable Lint::icrt_export(const std::vector<VarTable> &tables, const VarTable &vt, const Term &in_where) {
    const auto in = pair(element(in_where, 0), loc(element(in_where, 1)).second);
    const auto merged = merged_entries(*this, tables);
    VarTable out;
    auto old = vt.begin();
    for (std::size_t at = 0; at < merged.size();) {
        const auto name = merged[at].first.entry.name;
        const auto end = group_end(merged, at);
        old = skip_older(old, vt.end(), name);
        const std::span group(merged.begin() + static_cast<std::ptrdiff_t>(at),
                              merged.begin() + static_cast<std::ptrdiff_t>(end));
        if (old != vt.end() && old->name.word() == name.word()) {
            out.push_back(reused_variable(group, *old));
            ++old;
        } else {
            out.push_back(new_variable(group, in, tables.size()));
        }
        at = end;
    }
    return out;
}

VarTable Lint::handle_comprehension(const Comprehension &comprehension, const VarTable &vt0) {
    const auto [vt1, uvt] = lc_quals(comprehension.qualifiers, vt0);
    const auto evt = comprehension_exprs(comprehension.template_, vt1);
    const auto vt2 = vtupdate(evt, vt1);
    static_cast<void>(check_old_unused_vars(vt2, uvt));
    static_cast<void>(check_unused_vars(uvt, vt0));
    static_cast<void>(check_unused_vars(vt2, vt0));
    const auto vt3 = vtmerge(vtnew(vt2, OldVars{uvt}), uvt);
    return vt_no_unsafe(vt_no_unused(vtold(vt3, OldVars{vt0})));
}

VarTable Lint::comprehension_exprs(const Term &template_, const VarTable &vt) {
    if (!template_.is_list()) {
        return comprehension_expr(template_, vt);
    }
    VarTable merged;
    for (const auto &item : forms::elements(template_)) {
        merged = vtmerge_pat(comprehension_expr(item, vt), merged);
    }
    return merged;
}

VarTable Lint::comprehension_expr(const Term &node, const VarTable &vt) {
    if (is_node(node, "map_field_assoc", 4) || is_node(node, "map_field_exact", 4)) {
        if (is_node(node, "map_field_exact", 4)) {
            add_error({element(node, 1), atom("illegal_map_exact_in_comprehension"_atom)});
        }
        const std::vector parts{element(node, 2), element(node, 3)};
        return expr_list(parts, vt);
    }
    return expr(node, vt);
}

std::pair<VarTable, VarTable> Lint::lc_quals(const Term &qualifiers, const VarTable &vt0) {
    const auto outer = recdef_top_;
    recdef_top_ = false;
    auto vt = vt0;
    VarTable uvt;
    for (const auto &qualifier : forms::elements(qualifiers)) {
        lc_qual(qualifier, vt, uvt);
    }
    recdef_top_ = outer;
    return {vt, uvt};
}

void Lint::lc_qual(const Term &qualifier, VarTable &vt, VarTable &uvt) {
    if (is_node(qualifier, "zip", 3)) {
        const auto generators = filter_generators(element(qualifier, 2));
        if (!generators.empty()) {
            std::tie(vt, uvt) = handle_generators(generators, vt, uvt);
        }
    } else if (is_generator(qualifier)) {
        if (forms::tag_of(qualifier).starts_with("b_")) {
            bitstring_generator_pattern(element(qualifier, 2));
        }
        std::tie(vt, uvt) =
            handle_generator({.pattern = element(qualifier, 2), .source = element(qualifier, 3)}, vt, uvt, true);
    } else if (is_node(qualifier, "match", 4)) {
        compr_assign(qualifier, vt, uvt);
    } else {
        const auto fvt =
            is_guard_test(build_.context(), qualifier, guard_scope()) ? guard_test(qualifier, vt) : expr(qualifier, vt);
        vt = vtupdate(fvt, vt);
    }
}

// P = E as a qualifier: a strict generator over [E] with the compr_assign feature, else an error.
void Lint::compr_assign(const Term &qualifier, VarTable &vt, VarTable &uvt) {
    const auto enabled =
        std::ranges::any_of(features_, [](const Term &feature) { return is_atom(feature, "compr_assign"); });
    if (!enabled) {
        add_error({element(qualifier, 1), atom("compr_assign"_atom)});
        vt = vtupdate(expr(qualifier, vt), vt);
        return;
    }
    auto pattern = element(qualifier, 2);
    auto source = element(qualifier, 3);
    while (is_node(source, "match", 4)) {
        pattern = build_.node("match"_atom, element(source, 1), pattern, element(source, 2));
        source = element(source, 3);
    }
    std::tie(vt, uvt) = handle_generator({.pattern = pattern, .source = source}, vt, uvt, false);
}

// The generators of a zip; anything else is an error and left out.
std::vector<Term> Lint::filter_generators(const Term &qualifiers) {
    std::vector<Term> generators;
    for (const auto &qualifier : forms::elements(qualifiers)) {
        if (is_generator(qualifier)) {
            generators.push_back(qualifier);
        } else {
            add_error({element(qualifier, 1), atom("illegal_zip_generator"_atom)});
        }
    }
    return generators;
}

std::pair<VarTable, VarTable> Lint::handle_generators(const std::vector<Term> &generators, const VarTable &vt,
                                                      const VarTable &uvt) {
    std::vector<Term> patterns;
    std::vector<Term> sources;
    for (const auto &generator : generators) {
        patterns.push_back(element(generator, 2));
        sources.push_back(element(generator, 3));
    }
    const auto evt = exprs(build_.list(sources), vt);
    return generated(build_.list(patterns), evt, vt, uvt);
}

std::pair<VarTable, VarTable> Lint::handle_generator(const Generator &generator, const VarTable &vt,
                                                     const VarTable &uvt, bool check_unused_source) {
    const auto evt = expr(generator.source, vt);
    if (!check_unused_source) {
        const auto vt1 = vtupdate(vtold(evt, OldVars{vt}), vt);
        return generated_from(generator.pattern, vt1, uvt);
    }
    return generated(generator.pattern, evt, vt, uvt);
}

// The generators' sources checked (their own variables forgotten), then their patterns.
std::pair<VarTable, VarTable> Lint::generated(const Term &pattern, const VarTable &evt, const VarTable &vt,
                                              const VarTable &uvt) {
    const auto vt1 = vtupdate(vtold(evt, OldVars{vt}), vt);
    static_cast<void>(check_unused_vars(evt, vt));
    return generated_from(pattern, vt1, uvt);
}

std::pair<VarTable, VarTable> Lint::generated_from(const Term &pattern, const VarTable &vt1, const VarTable &uvt) {
    const auto found = comprehension_pattern(pattern, vt1);
    const auto vt2 = vtupdate(found.vt, vt1);
    shadow_vars(found.fresh, vt1, atom("generate"_atom));
    const auto svt = vtold(vt2, OldVars{found.fresh});
    static_cast<void>(check_old_unused_vars(svt, uvt));
    const auto nuvt = vtupdate(vtnew(svt, OldVars{uvt}), uvt);
    const auto vt3 = vtupdate(vtnew(vt2, OldVars{found.fresh}), found.fresh);
    return {vt3, nuvt};
}

PatternVars Lint::comprehension_pattern(const Term &pattern, const VarTable &vt) {
    if (pattern.is_cons()) {
        std::vector<Term> plain;
        std::vector<Term> keys;
        std::vector<Term> values;
        for (const auto &item : forms::elements(pattern)) {
            if (is_node(item, "map_field_exact", 4)) {
                keys.push_back(element(item, 2));
                values.push_back(element(item, 3));
            } else {
                plain.push_back(item);
            }
        }
        plain.insert(plain.end(), keys.begin(), keys.end());
        plain.insert(plain.end(), values.begin(), values.end());
        return pattern_list(plain, vt, {});
    }
    if (is_node(pattern, "map_field_exact", 4)) {
        const std::vector parts{element(pattern, 2), element(pattern, 3)};
        return pattern_list(parts, vt, {});
    }
    return this->pattern(pattern, vt, {});
}

// handle_bitstring_gen_pat/2: the last segment of a bit string generator's pattern must have a size.
void Lint::bitstring_generator_pattern(const Term &pattern) {
    if (!is_node(pattern, "bin", 3) || !element(pattern, 2).is_cons()) {
        return;
    }
    const auto segment = forms::elements(element(pattern, 2)).back();
    if (!is_node(segment, "bin_element", 5) || !is_atom(element(segment, 3), "default") ||
        !element(segment, 4).is_list()) {
        return;
    }
    const auto flags = forms::elements(element(segment, 4));
    if (std::ranges::any_of(flags, [](const Term &flag) {
            return is_atom(flag, "binary") || is_atom(flag, "bytes") || is_atom(flag, "bits") ||
                   is_atom(flag, "bitstring");
        })) {
        add_error({element(segment, 1), atom("unsized_binary_in_bin_gen_pattern"_atom)});
    }
}
} // namespace clause::runtime::builtins::front_end::lint
