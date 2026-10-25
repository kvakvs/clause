#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>

// erl_lint's patterns and guards (plan 21 step LIB34C): pattern/4 with the variables a pattern uses and binds, binary
// segments and their types, guard tests and guard expressions.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

std::size_t size_of(const Term &node) { return node.is_tuple() ? node.tuple_size_unchecked() : 0; }

bool only_vars(const Term &pattern) {
    if (is_node(pattern, "var", 3)) {
        return true;
    }
    return is_node(pattern, "match", 4) && only_vars(element(pattern, 2)) && only_vars(element(pattern, 3));
}

// is_gexpr_op/2.
bool guard_operator(const Term &op, std::int64_t arity) {
    if (!op.is_atom()) {
        return false;
    }
    const auto type = operator_type(op.atom_spelling_unchecked(), arity);
    return type == "arith" || type == "bool" || type == "comp";
}

std::int64_t arity_of(const std::vector<Term> &args) { return static_cast<std::int64_t>(args.size()); }

bool is_float_zero(const Term &node) {
    if (!is_node(node, "float", 3) || !element(node, 2).is_float()) {
        return false;
    }
    return element(node, 2).float_value_unchecked() == 0.0;
}

// Whether the node has `size` elements and one of the tags.
bool is_one_of_nodes(const Term &node, std::size_t size, std::initializer_list<std::string_view> tags) {
    return size_of(node) == size && forms::is_one_of(forms::tag_of(node), tags);
}

// Whether the node is the atom `name`.
bool is_atom_node(const Term &node, std::string_view name) {
    return is_node(node, "atom", 3) && is_atom(element(node, 2), name);
}

// A bound variable that was used, with the annotations of its binding.
VarInfo used_info(const std::vector<Term> &annos) { return var_info(VarKind::bound, Term{}, true, annos); }

// Whether a bit size that is not an integer is one of the literals that cannot be a size.
bool is_non_integer_size(const Term &folded) {
    return is_one_of_nodes(folded, 3, {"atom", "bin", "float", "map", "tuple"}) ||
           is_one_of_nodes(folded, 4, {"cons"}) || is_node(folded, "nil", 2);
}

// Whether `bits` is a size a float segment can have.
bool is_float_bits(std::int64_t bits) { return bits == 16 || bits == 32 || bits == 64; }

// The guard is_record callee: erlang:is_record with two or three arguments is is_record.
Term is_record_guard_callee(const Term &callee, std::size_t arity) {
    const auto erlang_is_record = is_node(callee, "remote", 4) && is_atom_node(element(callee, 2), "erlang") &&
                                  is_atom_node(element(callee, 3), "is_record");
    return erlang_is_record && (arity == 2 || arity == 3) ? element(callee, 3) : callee;
}
} // namespace

PatternVars Lint::pattern(const Term &node, const VarTable &vt, const VarTable &old) {
    if (is_node(node, "var", 3)) {
        return is_atom(element(node, 2), "_") ? PatternVars{} : pat_var(element(node, 2), element(node, 1), old, {});
    }
    if (is_one_of_nodes(node, 3, {"char", "integer", "string"}) || is_node(node, "nil", 2)) {
        return {};
    }
    if (is_node(node, "float", 3)) {
        if (is_float_zero(node)) {
            maybe_add_warning(element(node, 1), atom("match_float_zero"_atom));
        }
        return {};
    }
    if (is_node(node, "atom", 3)) {
        keyword_warning(element(node, 1), element(node, 2));
        return {};
    }
    return compound_pattern(node, vt, old);
}

PatternVars Lint::compound_pattern(const Term &node, const VarTable &vt, const VarTable &old) {
    if (is_node(node, "cons", 4)) {
        const auto head = pattern(element(node, 2), vt, old);
        const auto tail = pattern(element(node, 3), vt, old);
        const auto merged = vtmerge_pat(head.vt, tail.vt);
        return {.vt = merged, .fresh = vtmerge_pat(head.fresh, tail.fresh)};
    }
    if (is_node(node, "tuple", 3)) {
        return pattern_list(forms::elements(element(node, 2)), vt, old);
    }
    if (is_node(node, "map", 3)) {
        return pattern_map(element(node, 2), vt, old);
    }
    if (is_one_of_nodes(node, 4, {"record_index", "record"})) {
        return record_pattern(node, vt, old);
    }
    if (is_node(node, "bin", 3)) {
        return pattern_bin(element(node, 2), vt, old);
    }
    if (is_node(node, "match", 4)) {
        return match_pattern(node, vt, old);
    }
    if (is_node(node, "op", 5) && is_atom(element(node, 2), "++")) {
        return append_pattern(node, vt, old);
    }
    if (!is_pattern_expr(node)) {
        add_error({element(node, 1), atom("illegal_pattern"_atom)});
    }
    return {};
}

// Prefix ++ Rest: the prefix a literal string or a list of characters and integers, else an illegal pattern at the
// annotation of the last list cell taken (OTP rewrites the pattern one cell at a time).
PatternVars Lint::append_pattern(const Term &node, const VarTable &vt, const VarTable &old) {
    auto anno = element(node, 1);
    auto prefix = element(node, 3);
    while (is_node(prefix, "cons", 4) &&
           (is_node(element(prefix, 2), "char", 3) || is_node(element(prefix, 2), "integer", 3))) {
        anno = element(prefix, 1);
        prefix = element(prefix, 3);
    }
    if (is_node(prefix, "nil", 2) || is_node(prefix, "string", 3)) {
        return pattern(element(node, 4), vt, old);
    }
    add_error({anno, atom("illegal_pattern"_atom)});
    return {};
}

PatternVars Lint::match_pattern(const Term &node, const VarTable &vt, const VarTable &old) {
    const auto left = pattern(element(node, 2), vt, old);
    const auto right = pattern(element(node, 3), vt, old);
    const auto merged = vtmerge_pat(left.vt, right.vt);
    const auto fresh = vtmerge_pat(left.fresh, right.fresh);
    if (!only_vars(element(node, 2)) && !only_vars(element(node, 3))) {
        maybe_add_warning(element(node, 1), atom("match_alias_pats"_atom));
    }
    return {.vt = merged, .fresh = fresh};
}

PatternVars Lint::pattern_list(const std::vector<Term> &patterns, const VarTable &vt, const VarTable &old) {
    PatternVars all;
    for (const auto &item : patterns) {
        const auto found = pattern(item, vt, old);
        const auto merged = vtmerge_pat(found.vt, all.vt);
        all.fresh = vtmerge_pat(all.fresh, found.fresh);
        all.vt = merged;
    }
    return all;
}

PatternVars Lint::head(const Term &patterns, const VarTable &vt, const VarTable &old) {
    std::vector<PatternVars> each;
    for (const auto &item : forms::elements(patterns)) {
        each.push_back(pattern(item, vt, old));
    }
    PatternVars all;
    for (auto at = each.rbegin(); at != each.rend(); ++at) {
        const auto merged = vtmerge_pat(at->vt, all.vt);
        all.fresh = vtmerge_pat(at->fresh, all.fresh);
        all.vt = merged;
    }
    return all;
}

// A variable first bound in a pattern.
PatternVars Lint::new_pattern_var(const Term &name, const Term &anno) {
    if (recdef_top_) {
        add_error({anno, pair(atom("variable_in_record_def"_atom), name)});
    }
    return {.vt = {}, .fresh = {{.name = name, .info = var_info(VarKind::bound, Term{}, false, {anno})}}};
}

// A variable of an enclosing scope matched again in a pattern: the errors and warnings of the reuse.
void Lint::warn_reused_pattern_var(const Term &name, const Term &anno, const VarInfo &found) {
    if (found.kind == VarKind::unsafe) {
        add_error({anno, triple(atom("unsafe_var"_atom), name, found.where)});
    } else {
        warn_underscore_match(name, anno);
    }
    if (found.kind != VarKind::exported) {
        return;
    }
    if (export_var_subexpr(found.where)) {
        maybe_add_warning(anno, triple(atom("export_var_subexpr"_atom), name, found.where));
    } else {
        add_warning({anno, triple(atom("exported_var"_atom), name, found.where)});
    }
}

PatternVars Lint::pat_var(const Term &name, const Term &anno, const VarTable &old, const VarTable &fresh) {
    if (const auto *found = vtfind(fresh, name); found != nullptr && found->kind == VarKind::bound) {
        warn_underscore_match(name, anno);
        return {.vt = {}, .fresh = {{.name = name, .info = used_info(found->annos)}}};
    }
    const auto *found = vtfind(old, name);
    if (found == nullptr) {
        return new_pattern_var(name, anno);
    }
    warn_reused_pattern_var(name, anno, *found);
    return {.vt = {{.name = name, .info = used_info(found->annos)}}, .fresh = {}};
}

PatternVars Lint::pat_binsize_var(const Term &name, const Term &anno, const VarTable &vt, const VarTable &fresh) {
    if (const auto *found = vtfind(fresh, name); found != nullptr && found->kind == VarKind::bound) {
        return {.vt = {}, .fresh = {{.name = name, .info = used_info(found->annos)}}};
    }
    const auto *found = vtfind(vt, name);
    if (found == nullptr) {
        unbound_var(name, anno, vt);
        return {.vt = {{.name = name, .info = used_info({anno})}}, .fresh = {}};
    }
    if (found->kind == VarKind::unsafe) {
        add_error({anno, triple(atom("unsafe_var"_atom), name, found->where)});
    } else if (found->kind == VarKind::exported && export_var_subexpr(found->where)) {
        maybe_add_warning(anno, triple(atom("export_var_subexpr"_atom), name, found->where));
    } else if (found->kind == VarKind::exported && warn_enabled("export_vars")) {
        add_warning({anno, triple(atom("exported_var"_atom), name, found->where)});
    }
    return {.vt = {{.name = name, .info = used_info(found->annos)}}, .fresh = {}};
}

PatternVars Lint::pattern_map(const Term &fields, const VarTable &vt, const VarTable &old) {
    PatternVars all;
    for (const auto &field : forms::elements(fields)) {
        if (is_node(field, "map_field_assoc", 4)) {
            add_error({element(field, 1), atom("illegal_map_assoc_in_pattern"_atom)});
            continue;
        }
        if (!is_node(field, "map_field_exact", 4)) {
            lists::no_clause();
        }
        gexpr_context_ = GexprContext::map_key;
        const auto kvt = gexpr(element(field, 2), vt);
        const auto value = pattern(element(field, 3), vt, old);
        const auto vt1 = vtmerge_pat(kvt, value.vt);
        const auto vt2 = vtmerge_pat(vt1, all.vt);
        all.fresh = vtmerge_pat(all.fresh, value.fresh);
        all.vt = vt2;
    }
    return all;
}

PatternVars Lint::pattern_bin(const Term &segments, const VarTable &vt, const VarTable &old) {
    SegmentFold fold{.vars = {}, .previous_size = build_.integer(0), .previous_anno = build_.integer(0)};
    for (const auto &segment : forms::elements(segments)) {
        if (!is_node(segment, "bin_element", 5)) {
            lists::no_clause();
        }
        const auto value = element(segment, 2);
        if (is_node(value, "string", 3) && !good_string_size_type(element(segment, 3), element(segment, 4))) {
            add_error({element(segment, 1), atom("typed_literal_string"_atom)});
            continue;
        }
        pattern_element(segment, vt, OldVars{old}, fold);
    }
    return fold.vars;
}

bool Lint::good_string_size_type(const Term &size, const Term &types) const {
    if (!is_atom(size, "default")) {
        return false;
    }
    if (is_atom(types, "default")) {
        return true;
    }
    return std::ranges::any_of(forms::elements(types), [](const Term &type) {
        return is_atom(type, "utf8") || is_atom(type, "utf16") || is_atom(type, "utf32");
    });
}

void Lint::pattern_element(const Term &segment, const VarTable &vt, OldVars old, SegmentFold &fold) {
    const auto anno = element(segment, 1);
    const auto value = element(segment, 2);
    const auto expr_vars = pat_bit_expr(value, old.table, fold.vars.fresh);
    auto size_vars = pat_bit_size(element(segment, 3), vt, fold.vars.fresh);
    const auto settled = bit_type({anno, size_vars.size}, element(segment, 4));
    auto checked = bit_size_check(anno, settled);
    if (is_node(value, "string", 3) && is_atom(checked, "all")) {
        checked = build_.integer(8 * static_cast<std::int64_t>(forms::elements(element(value, 2)).size()));
    }
    if (is_atom(fold.previous_size, "all")) {
        add_error({fold.previous_anno, atom("unsized_binary_not_at_end"_atom)});
    }
    fold.previous_size = checked;
    fold.previous_anno = anno;
    fold.vars.vt = vtmerge(size_vars.vars.vt, vtmerge(expr_vars.vt, fold.vars.vt));
    fold.vars.fresh = vtmerge(size_vars.vars.fresh, vtmerge(fold.vars.fresh, expr_vars.fresh));
}

PatternVars Lint::pat_bit_expr(const Term &node, const VarTable &old, const VarTable &fresh) {
    if (is_node(node, "var", 3)) {
        return is_atom(element(node, 2), "_") ? PatternVars{} : pat_var(element(node, 2), element(node, 1), old, fresh);
    }
    if (is_node(node, "string", 3)) {
        return {};
    }
    if (is_node(node, "bin", 3) || !is_pattern_expr(node)) {
        add_error({element(node, 1), atom("illegal_pattern"_atom)});
    }
    return {};
}

SizeVars Lint::pat_bit_size(const Term &size, const VarTable &vt, const VarTable &fresh) {
    if (is_atom(size, "default")) {
        return {.size = size, .vars = {}};
    }
    if (is_node(size, "var", 3)) {
        return {.size = atom("unknown"_atom), .vars = pat_binsize_var(element(size, 2), element(size, 1), vt, fresh)};
    }
    const auto anno = element(size, 1);
    const auto folded = partial_eval(size);
    if (is_node(folded, "integer", 3) && collections::same(element(folded, 1), anno)) {
        return {.size = element(folded, 2), .vars = {}};
    }
    const auto context = gexpr_context_;
    bvt_ = fresh;
    gexpr_context_ = GexprContext::bin_seg_size;
    const auto used = gexpr(size, vt);
    const auto bound = std::move(*bvt_);
    bvt_.reset();
    gexpr_context_ = context;
    if (is_non_integer_size(folded)) {
        add_warning({anno, atom("non_integer_bitsize"_atom)});
    }
    return {.size = atom("unknown"_atom), .vars = {.vt = used, .fresh = bound}};
}

VarTable Lint::expr_bin(const Term &segments, const VarTable &vt, bool guard_context) {
    VarTable merged;
    for (const auto &segment : forms::elements(segments)) {
        if (!is_node(segment, "bin_element", 5)) {
            lists::no_clause();
        }
        const auto anno = element(segment, 1);
        const auto vt1 = guard_context ? gexpr(element(segment, 2), vt) : expr(element(segment, 2), vt);
        const auto [size, vt2] = bit_size(element(segment, 3), vt, guard_context);
        static_cast<void>(bit_size_check(anno, bit_type({anno, size}, element(segment, 4))));
        const std::array all{vt2, vt1, merged};
        merged = vtmerge(all);
    }
    return merged;
}

std::pair<Term, VarTable> Lint::bit_size(const Term &size, const VarTable &vt, bool guard_context) {
    if (is_atom(size, "default")) {
        return {size, {}};
    }
    if (is_node(size, "atom", 3) && is_atom(element(size, 2), "all")) {
        return {atom("all"_atom), {}};
    }
    if (is_guard_expr(build_.context(), size, guard_scope())) {
        const auto folded = partial_eval(size);
        if (is_node(folded, "integer", 3)) {
            return {element(folded, 2), {}};
        }
    }
    return {atom("unknown"_atom), guard_context ? gexpr(size, vt) : expr(size, vt)};
}

BitTypeResult Lint::bit_type(const AnnoTerm &size, const Term &types) {
    auto settled = set_bit_type(*this, size.term, types);
    if (settled.error) {
        add_error({size.anno, *settled.error});
        settled = set_bit_type(*this, atom("default"_atom), build_.nil());
    }
    return settled;
}

Term Lint::bit_size_check(const Term &anno, const BitTypeResult &settled) {
    const auto &size = settled.size;
    if (is_atom(size, "unknown") || is_atom(size, "undefined")) {
        return size;
    }
    if (is_atom(size, "all")) {
        if (settled.binary_type) {
            return size;
        }
        add_error({anno, atom("illegal_bitsize"_atom)});
        return atom("unknown"_atom);
    }
    const auto value = string::small_of(size);
    if (!value || !settled.unit) {
        return size;
    }
    const auto bits = *settled.unit * *value;
    if (settled.float_type && !is_float_bits(bits)) {
        add_warning({anno, pair(atom("bad_bitsize"_atom), chars("float"))});
    }
    return build_.integer(bits);
}

VarTable Lint::guard(const Term &guard, const VarTable &vt) {
    if (!guard.is_cons() || !guard.head_unchecked().is_list()) {
        return guard_tests(guard, vt);
    }
    std::vector<VarTable> each;
    auto current = vt;
    for (const auto &conjunction : forms::elements(guard)) {
        each.push_back(guard_tests(conjunction, current));
        current = vtupdate(each.back(), current);
    }
    VarTable result;
    for (auto at = each.rbegin(); at != each.rend(); ++at) {
        result = vtupdate(*at, result);
    }
    return result;
}

VarTable Lint::guard_tests(const Term &tests, const VarTable &vt) {
    std::vector<VarTable> each;
    auto current = vt;
    for (const auto &test : forms::elements(tests)) {
        each.push_back(guard_test(test, current));
        current = vtupdate(each.back(), current);
    }
    VarTable result;
    for (auto at = each.rbegin(); at != each.rend(); ++at) {
        result = vtupdate(*at, result);
    }
    return result;
}

VarTable Lint::guard_test(const Term &test, const VarTable &vt) {
    obsolete_guard(test);
    if (!is_node(test, "call", 4) || !is_node(element(test, 2), "atom", 3)) {
        return gexpr(test, vt);
    }
    const auto anno = element(test, 1);
    const auto callee = element(test, 2);
    const auto name = element(callee, 2);
    const auto args = forms::elements(element(test, 3));
    if (is_atom(name, "record") && args.size() == 2) {
        return gexpr(
            build_.node("call"_atom, anno, build_.atom_node(element(callee, 1), "is_record"_atom), element(test, 3)),
            vt);
    }
    const auto errors = errors_.size();
    const auto warnings = warnings_.size();
    auto asvt = gexpr_list(args, vt);
    const auto arity = arity_of(args);
    if (name.is_atom() && is_type_test(name.atom_spelling_unchecked(), arity) && !is_atom(name, "is_record") &&
        arity != 2) {
        if (!no_guard_bif_clash(fa(name, arity))) {
            add_error({anno, pair(atom("illegal_guard_local_call"_atom), fa(name, arity))});
        }
        return asvt;
    }
    // gexpr/3 runs from the state before the arguments were checked.
    errors_.resize(errors);
    warnings_.resize(warnings);
    return gexpr(test, vt);
}

void Lint::obsolete_guard(const Term &test) {
    if (!is_node(test, "call", 4) || !is_node(element(test, 2), "atom", 3) || !element(element(test, 2), 2).is_atom()) {
        return;
    }
    const auto anno = element(test, 1);
    const auto name_anno = element(element(test, 2), 1);
    const auto name = element(element(test, 2), 2);
    const auto arity = arity_of(forms::elements(element(test, 3)));
    if (!is_old_type_test(name.atom_spelling_unchecked(), arity)) {
        deprecated_function(anno, {atom("erlang"_atom), name}, arity);
        return;
    }
    maybe_add_warning(name_anno, pair(atom("obsolete_guard"_atom), fa(name, arity)));
    const auto modern = build_.atom(std::string("is_") + std::string(name.atom_spelling_unchecked()));
    if (is_local_function(fa(modern, arity))) {
        add_error({name_anno, pair(atom("obsolete_guard_overridden"_atom), name)});
    }
}

VarTable Lint::gexpr_list(const std::vector<Term> &list, const VarTable &vt) {
    VarTable merged;
    for (const auto &item : list) {
        merged = vtmerge(gexpr(item, vt), merged);
    }
    return merged;
}

VarTable Lint::gexpr(const Term &node, const VarTable &vt) {
    if (is_node(node, "var", 3)) {
        return expr_var(element(node, 2), element(node, 1), vt);
    }
    if (is_one_of_nodes(node, 3, {"char", "integer", "float", "string"}) || is_node(node, "nil", 2)) {
        return {};
    }
    if (is_node(node, "atom", 3)) {
        keyword_warning(element(node, 1), element(node, 2));
        return {};
    }
    if (is_node(node, "cons", 4)) {
        const std::vector parts{element(node, 2), element(node, 3)};
        return gexpr_list(parts, vt);
    }
    if (is_node(node, "tuple", 3)) {
        return gexpr_list(forms::elements(element(node, 2)), vt);
    }
    if (is_node(node, "map", 3)) {
        check_assoc_fields(element(node, 2));
        return map_fields({}, vt, element(node, 2), true);
    }
    if (is_node(node, "map", 4)) {
        const auto svt = gexpr(element(node, 2), vt);
        return vtmerge(svt, map_fields({}, vt, element(node, 3), true));
    }
    return gexpr_compound(node, vt);
}

// A unary operator in a guard: its operand, and an error unless the operator is a guard one.
VarTable Lint::gexpr_unary(const Term &node, const VarTable &vt) {
    auto avt = gexpr(element(node, 3), vt);
    if (!guard_operator(element(node, 2), 1)) {
        add_error({element(node, 1), atom("illegal_guard_expr"_atom)});
    }
    return avt;
}

VarTable Lint::gexpr_compound(const Term &node, const VarTable &vt) {
    if (is_one_of_nodes(node, 4, {"record_index", "record"}) || is_node(node, "record_field", 5)) {
        return record_expr(node, vt, true);
    }
    if (is_node(node, "bin", 3)) {
        return expr_bin(element(node, 2), vt, true);
    }
    if (is_node(node, "call", 4)) {
        return gexpr_call(node, vt);
    }
    if (is_node(node, "op", 4)) {
        return gexpr_unary(node, vt);
    }
    if (is_node(node, "op", 5)) {
        return gexpr_binary(node, vt);
    }
    add_error({node.is_tuple() && size_of(node) >= 2 ? element(node, 1) : node, atom("illegal_guard_expr"_atom)});
    return {};
}

VarTable Lint::gexpr_binary(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto op = element(node, 2);
    const std::vector parts{element(node, 3), element(node, 4)};
    if (is_atom(op, "andalso") || is_atom(op, "orelse")) {
        return gexpr_list(parts, vt);
    }
    if (is_atom(op, "=:=") || is_atom(op, "=/=")) {
        expr_check_match_zero(element(node, 3));
        expr_check_match_zero(element(node, 4));
        return gexpr_list(parts, vt);
    }
    const auto avt = gexpr_list(parts, vt);
    if (guard_operator(op, 2)) {
        warn_obsolete_op({anno, op});
    } else {
        add_error({anno, atom("illegal_guard_expr"_atom)});
    }
    return avt;
}

// A call of a local function in a guard: only guard BIFs that no function clashes with are allowed.
VarTable Lint::gexpr_local_call(const AnnoTerm &callee, const std::vector<Term> &args, const VarTable &vt) {
    const auto &name = callee.term;
    auto asvt = gexpr_list(args, vt);
    const auto arity = arity_of(args);
    const auto key = fa(name, arity);
    const auto guard_bif = name.is_atom() && is_guard_bif(name.atom_spelling_unchecked(), arity);
    if (!(guard_bif && no_guard_bif_clash(key))) {
        add_error({callee.anno, is_local_function(key) || is_imported_function(key)
                                    ? pair(atom("illegal_guard_local_call"_atom), key)
                                    : atom("illegal_guard_expr"_atom)});
    }
    return asvt;
}

// A call of erlang:Name in a guard: a guard BIF or operator.
VarTable Lint::gexpr_erlang_call(const AnnoTerm &callee, const std::vector<Term> &args, const VarTable &vt) {
    const auto &name = callee.term;
    auto asvt = gexpr_list(args, vt);
    const auto arity = arity_of(args);
    const auto guard_bif = name.is_atom() && is_guard_bif(name.atom_spelling_unchecked(), arity);
    if (!guard_bif && !guard_operator(name, arity)) {
        add_error({callee.anno, atom("illegal_guard_expr"_atom)});
    }
    return asvt;
}

VarTable Lint::gexpr_call(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto args = forms::elements(element(node, 3));
    const auto callee = is_record_guard_callee(element(node, 2), args.size());
    if (is_atom_node(callee, "is_record") && (args.size() == 2 || args.size() == 3)) {
        return gexpr_is_record(anno, args, vt);
    }
    if (is_node(callee, "atom", 3)) {
        return gexpr_local_call({anno, element(callee, 2)}, args, vt);
    }
    if (is_node(callee, "remote", 4) && is_atom_node(element(callee, 2), "erlang") &&
        is_node(element(callee, 3), "atom", 3)) {
        return gexpr_erlang_call({anno, element(element(callee, 3), 2)}, args, vt);
    }
    add_error({anno, atom("illegal_guard_expr"_atom)});
    return {};
}

// is_record/2,3 in a guard: a literal record name is checked, anything else is illegal.
VarTable Lint::gexpr_is_record(const Term &anno, const std::vector<Term> &args, const VarTable &vt) {
    if (args.size() == 2) {
        if (is_node(args[1], "atom", 3)) {
            const auto rvt = gexpr(args[0], vt);
            exist_any_record(element(args[1], 1), element(args[1], 2));
            return rvt;
        }
        const auto asvt = gexpr_list(args, vt);
        add_error({anno, atom("illegal_guard_expr"_atom)});
        return asvt;
    }
    const auto third = forms::tag_of(args[2]);
    if (is_node(args[1], "atom", 3) && size_of(args[2]) == 3 && (third == "integer" || third == "atom")) {
        const auto key = fa(atom("is_record"_atom), 3);
        const auto clash = !no_guard_bif_clash(key);
        const auto evt = gexpr(args[0], vt);
        if (clash) {
            add_error({anno, pair(atom("illegal_guard_local_call"_atom), key)});
        }
        return evt;
    }
    const auto asvt = gexpr_list(args, vt);
    add_error({anno, atom("illegal_guard_expr"_atom)});
    return asvt;
}

void Lint::expr_check_match_zero(const Term &node) {
    if (is_float_zero(node)) {
        maybe_add_warning(element(node, 1), atom("match_float_zero"_atom));
    } else if (is_node(node, "cons", 4)) {
        expr_check_match_zero(element(node, 3));
        expr_check_match_zero(element(node, 2));
    } else if (is_node(node, "tuple", 3)) {
        for (const auto &item : forms::elements(element(node, 2))) {
            expr_check_match_zero(item);
        }
    }
}

void Lint::warn_obsolete_op(const AnnoTerm &op) {
    if (is_atom(op.term, "and")) {
        maybe_add_warning(op.anno, triple(atom("obsolete_bool_op"_atom), chars("'and'"), chars("'andalso'")));
    } else if (is_atom(op.term, "or")) {
        maybe_add_warning(op.anno, triple(atom("obsolete_bool_op"_atom), chars("'or'"), chars("'orelse'")));
    }
}
} // namespace clause::runtime::builtins::front_end::lint
