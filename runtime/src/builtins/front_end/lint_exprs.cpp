#include "abstract.hpp"
#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>

// erl_lint's expressions (plan 21 step LIB34C): expr/3 and exprs/3 returning the variables each uses and binds, the
// exports and unsafe marks of compound expressions, calls and the checks on them, funs, and the variable warnings.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

std::size_t size_of(const Term &node) { return node.is_tuple() ? node.tuple_size_unchecked() : 0; }

// Whether the variable's name starts with an underscore.
bool underscored(const Term &name) { return name.atom_spelling_unchecked().starts_with('_'); }

// Whether the node is the atom `name`.
bool is_atom_node(const Term &node, std::string_view name) {
    return is_node(node, "atom", 3) && is_atom(element(node, 2), name);
}

// Whether the remote names module `module` and function `function`.
bool is_remote(const Remote &remote, std::string_view module, std::string_view function) {
    return is_atom(remote.module, module) && is_atom(remote.name, function);
}

// The annotations of a variable bound twice in one pattern that were not used yet.
std::vector<Term> unused_binding_annos(const VarInfo &left, const VarInfo &right, const VarInfo &merged) {
    if (!left.used && !right.used) {
        return merged.annos;
    }
    if (!left.used) {
        return left.annos;
    }
    return right.used ? std::vector<Term>{} : right.annos;
}

// Whether a nif-loading module's inline option is on: inline, or {inline, List}.
bool is_inline_option(const Term &option) {
    return is_atom(option, "inline") ||
           (is_pair(option) && is_atom(element(option, 0), "inline") && element(option, 1).is_cons());
}

// The warning category of an obsolete function of the kind.
std::string_view obsolete_flag(std::string_view kind, const Term &info) {
    if (kind == "deprecated") {
        return "deprecated_function";
    }
    if (kind == "removed") {
        return "removed";
    }
    const auto possibly = kind == "unsafe" && info.tuple_size_unchecked() != 3 && is_atom(element(info, 1), "possibly");
    return possibly ? "possibly_unsafe_function" : "unsafe_function";
}
} // namespace

// {Tag, Location} of a construct at `anno`.
Term Lint::where(const Term &tag, const Term &anno) const { return pair(tag, anno_location(build_.context(), anno)); }

VarTable Lint::exprs(const Term &list, const VarTable &vt) {
    std::vector<VarTable> each;
    auto current = vt;
    for (const auto &expr : forms::elements(list)) {
        each.push_back(this->expr(expr, current));
        current = vtupdate(each.back(), current);
    }
    VarTable result;
    for (auto at = each.rbegin(); at != each.rend(); ++at) {
        result = vtupdate(*at, result);
    }
    return result;
}

VarTable Lint::expr_list(const std::vector<Term> &list, const VarTable &vt) {
    VarTable merged;
    for (const auto &expr : list) {
        merged = vtmerge_pat(this->expr(expr, vt), merged);
    }
    return merged;
}

VarTable Lint::vtupd_export_expr_list(const Term &place, const std::vector<Term> &list, const VarTable &vt) {
    const auto evt = expr_list(list, vt);
    return vtupdate(vt_mark_new(VarKind::exported, place, evt, vt), evt);
}

VarTable Lint::vtupd_unsafe(const Term &place, const VarTable &fresh, const VarTable &old) {
    return vtupdate(vt_mark_new(VarKind::unsafe, place, fresh, old), fresh);
}

VarTable Lint::vtupd_export(const Term &place, const VarTable &fresh, const VarTable &old) {
    return vtupdate(vt_mark_new(VarKind::exported, place, fresh, old), fresh);
}

// A variable in both sides of a pattern merge: its merged entry, and the underscore match warnings.
VarEntry Lint::merge_pattern_vars(const VarEntry &left, const VarEntry &right) {
    auto info = left.info;
    merge_state(info, right.info);
    info.annos = merge_annos(left.info.annos, right.info.annos);
    info.used = true;
    const auto matched = unused_binding_annos(left.info, right.info, info);
    if ((!left.info.used || !right.info.used) && warn_enabled("underscore_match") && underscored(left.name)) {
        for (const auto &anno : matched) {
            add_warning({anno, pair(atom("match_underscore_var_pat"_atom), left.name)});
        }
    }
    return {.name = left.name, .info = info};
}

VarTable Lint::vtmerge_pat(const VarTable &left, const VarTable &right) {
    VarTable out;
    auto l = left.begin();
    auto r = right.begin();
    const auto less = [](const VarEntry &a, const VarEntry &b) {
        return a.name.atom_spelling_unchecked() < b.name.atom_spelling_unchecked();
    };
    while (l != left.end() || r != right.end()) {
        if (r == right.end() || (l != left.end() && less(*l, *r))) {
            out.push_back(*l++);
        } else if (l == left.end() || less(*r, *l)) {
            out.push_back(*r++);
        } else {
            out.push_back(merge_pattern_vars(*l++, *r++));
        }
    }
    return out;
}

VarTable Lint::expr(const Term &node, const VarTable &vt) {
    using Handler = VarTable (Lint::*)(const Term &, const VarTable &);

    struct Rule {
        std::string_view tag;
        std::size_t size;
        Handler handler;
    };

    static constexpr auto RULES = std::to_array<Rule>({
        {"var", 3, &Lint::expr_variable},
        {"char", 3, &Lint::expr_constant},
        {"integer", 3, &Lint::expr_constant},
        {"float", 3, &Lint::expr_constant},
        {"string", 3, &Lint::expr_constant},
        {"nil", 2, &Lint::expr_constant},
        {"executable_line", 3, &Lint::expr_constant},
        {"ssa_check_when", 7, &Lint::expr_constant},
        {"atom", 3, &Lint::expr_atom},
        {"cons", 4, &Lint::expr_cons},
        {"lc", 4, &Lint::expr_comprehension},
        {"bc", 4, &Lint::expr_comprehension},
        {"mc", 4, &Lint::expr_comprehension},
        {"tuple", 3, &Lint::expr_tuple},
        {"map", 3, &Lint::expr_map},
        {"map", 4, &Lint::expr_map_update},
        {"record_index", 4, &Lint::expr_record},
        {"record", 4, &Lint::expr_record},
        {"record_field", 5, &Lint::expr_record},
        {"record", 5, &Lint::expr_record},
        {"bin", 3, &Lint::expr_binary},
        {"block", 3, &Lint::expr_block},
        {"if", 3, &Lint::expr_if},
        {"case", 4, &Lint::expr_case},
        {"receive", 3, &Lint::expr_if},
        {"receive", 5, &Lint::expr_receive_after},
        {"fun", 3, &Lint::expr_fun},
        {"named_fun", 4, &Lint::expr_named_fun},
        {"call", 4, &Lint::expr_call},
        {"try", 6, &Lint::expr_try},
        {"catch", 3, &Lint::expr_catch},
        {"match", 4, &Lint::expr_match},
        {"maybe_match", 4, &Lint::expr_match},
        {"maybe", 3, &Lint::expr_maybe},
        {"maybe", 4, &Lint::expr_maybe_else},
        {"op", 4, &Lint::expr_unary},
        {"op", 5, &Lint::expr_binary_op},
        {"remote", 4, &Lint::expr_remote},
    });
    const auto tag = forms::tag_of(node);
    const auto size = size_of(node);
    for (const auto &rule : RULES) {
        if (rule.tag == tag && rule.size == size) {
            return (this->*rule.handler)(node, vt);
        }
    }
    lists::no_clause();
}

VarTable Lint::expr_variable(const Term &node, const VarTable &vt) {
    return expr_var(element(node, 2), element(node, 1), vt);
}

VarTable Lint::expr_constant(const Term & /*node*/, const VarTable & /*vt*/) { return {}; }

VarTable Lint::expr_atom(const Term &node, const VarTable & /*vt*/) {
    keyword_warning(element(node, 1), element(node, 2));
    return {};
}

VarTable Lint::expr_cons(const Term &node, const VarTable &vt) {
    const std::vector parts{element(node, 2), element(node, 3)};
    return vtupd_export_expr_list(where(atom("list"_atom), element(node, 1)), parts, vt);
}

VarTable Lint::expr_comprehension(const Term &node, const VarTable &vt) {
    return handle_comprehension({element(node, 2), element(node, 3)}, vt);
}

VarTable Lint::expr_tuple(const Term &node, const VarTable &vt) {
    return vtupd_export_expr_list(where(atom("tuple"_atom), element(node, 1)), forms::elements(element(node, 2)), vt);
}

// check_assoc_fields/2: := in a map construction.
void Lint::check_assoc_fields(const Term &fields) {
    for (const auto &field : forms::elements(fields)) {
        if (is_node(field, "map_field_exact", 4)) {
            add_error({element(field, 1), atom("illegal_map_construction"_atom)});
        } else if (!is_node(field, "map_field_assoc", 4)) {
            lists::no_clause();
        }
    }
}

// map_fields/4 with vtupd_export_expr_list over each field's key and value.
VarTable Lint::map_fields(const Term &place, const VarTable &vt, const Term &fields, bool guard_context) {
    std::vector<VarTable> each;
    for (const auto &field : forms::elements(fields)) {
        const auto tag = forms::tag_of(field);
        if ((tag != "map_field_assoc" && tag != "map_field_exact") || size_of(field) != 4) {
            lists::no_clause();
        }
        const std::vector parts{element(field, 2), element(field, 3)};
        each.push_back(guard_context ? gexpr_list(parts, vt) : vtupd_export_expr_list(place, parts, vt));
    }
    VarTable result;
    for (auto at = each.rbegin(); at != each.rend(); ++at) {
        result = vtupdate(*at, result);
    }
    return result;
}

VarTable Lint::expr_map(const Term &node, const VarTable &vt) {
    check_assoc_fields(element(node, 2));
    return map_fields(where(atom("map"_atom), element(node, 1)), vt, element(node, 2), false);
}

VarTable Lint::expr_map_update(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto place = where(atom("map"_atom), anno);
    const std::vector source{element(node, 2)};
    const auto svt = vtupd_export_expr_list(place, source, vt);
    const auto fvt = map_fields(place, vt, element(node, 3), false);
    warn_if_literal_update({anno, element(node, 2)});
    return vtupdate(svt, fvt);
}

VarTable Lint::expr_record(const Term &node, const VarTable &vt) { return record_expr(node, vt, false); }

VarTable Lint::expr_binary(const Term &node, const VarTable &vt) {
    const auto vt1 = expr_bin(element(node, 2), vt, false);
    return vtupd_export(where(atom("binary"_atom), element(node, 1)), vt1, vt);
}

VarTable Lint::expr_block(const Term &node, const VarTable &vt) {
    const auto vt1 = exprs(element(node, 2), vt);
    return vtupd_export(where(atom("begin"_atom), element(node, 1)), vt1, vt);
}

// {'if', A, Cs} and {'receive', A, Cs}.
VarTable Lint::expr_if(const Term &node, const VarTable &vt) {
    return icrt_clauses(element(node, 2), pair(element(node, 0), element(node, 1)), vt);
}

VarTable Lint::expr_case(const Term &node, const VarTable &vt) {
    const auto evt = expr(element(node, 2), vt);
    const auto cvt = icrt_clauses(element(node, 3), pair(atom("case"_atom), element(node, 1)), vtupdate(evt, vt));
    return vtmerge(evt, cvt);
}

VarTable Lint::expr_receive_after(const Term &node, const VarTable &vt) {
    const auto tvt = expr(element(node, 3), vt);
    const auto tevt = exprs(element(node, 4), vt);
    auto tables = icrt_clause_tables(element(node, 2), vt);
    tables.insert(tables.begin(), tevt);
    const auto rvt = icrt_export(tables, vt, pair(atom("receive"_atom), element(node, 1)));
    const std::array all{tvt, tevt, rvt};
    return vtmerge(all);
}

VarTable Lint::expr_fun(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto body = element(node, 2);
    if (is_node(body, "clauses", 2)) {
        return fun_clauses(element(body, 1), vt);
    }
    if (is_node(body, "function", 3)) {
        local_fun(anno, element(body, 1), element(body, 2));
        return {};
    }
    if (!is_node(body, "function", 4)) {
        lists::no_clause();
    }
    const auto module = element(body, 1);
    const auto function = element(body, 2);
    const auto arity = element(body, 3);
    if (is_node(module, "atom", 3) && is_node(function, "atom", 3) && is_node(arity, "integer", 3)) {
        check_remote_self_call(anno, {element(module, 2), element(function, 2)},
                               string::small_of(element(arity, 2)).value_or(-1));
        return {};
    }
    const std::vector parts{module, function, arity};
    return expr_list(parts, vt);
}

// fun F/A: record_info/2 is illegal, an import cannot be made a fun, others are checked like calls.
void Lint::local_fun(const Term &anno, const Term &name, const Term &arity_term) {
    if (is_atom(name, "record_info") && collections::same(arity_term, build_.integer(2))) {
        add_error({anno, atom("illegal_record_info"_atom)});
        return;
    }
    const auto arity = string::small_of(arity_term).value_or(-1);
    const auto key = pair(name, arity_term);
    if (is_imported_function(key)) {
        add_error({anno, pair(atom("fun_import"_atom), key)});
    } else {
        const std::vector<Term> args(static_cast<std::size_t>(std::max<std::int64_t>(arity, 0)),
                                     atom("undefined"_atom));
        check_call(anno, name, args, anno);
    }
    const auto bif = name.is_atom() && is_bif(name.atom_spelling_unchecked(), arity) && !autoimport_suppressed(key);
    if (is_local_function(key) || !(is_imported_function(key) || bif)) {
        call_function({anno, name}, arity);
    }
}

VarTable Lint::expr_named_fun(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2);
    if (is_atom(name, "_")) {
        return fun_clauses(element(node, 3), vt);
    }
    const VarTable nvt0{{.name = name, .info = var_info(VarKind::bound, Term{}, false, {anno})}};
    shadow_vars(nvt0, vt, atom("named fun"_atom));
    const auto nvt1 = vtupdate(vtnew(vt, OldVars{nvt0}), nvt0);
    const auto csvt = fun_clauses(element(node, 3), nvt1);
    static_cast<void>(check_unused_vars(vtupdate(csvt, nvt0), {}));
    return vtold(csvt, OldVars{vt});
}

// {erlang, is_record} as the callee is the remote call erlang:is_record, and its two-argument call is is_record.
Term Lint::is_record_callee(const Term &callee, std::size_t arity) {
    auto result = callee;
    if (is_node(result, "tuple", 3)) {
        const auto items = forms::elements(element(result, 2));
        if (items.size() == 2 && is_atom_node(items[0], "erlang") && is_atom_node(items[1], "is_record")) {
            result = build_.node("remote"_atom, element(result, 1), items[0], items[1]);
        }
    }
    if (is_node(result, "remote", 4) && is_atom_node(element(result, 2), "erlang") &&
        is_atom_node(element(result, 3), "is_record") && arity == 2) {
        result = element(result, 3);
    }
    return result;
}

VarTable Lint::expr_call(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto args = forms::elements(element(node, 3));
    const auto callee = is_record_callee(element(node, 2), args.size());
    if (is_atom_node(callee, "is_record") && args.size() == 2 && is_node(args[1], "atom", 3)) {
        const auto rvt = expr(args[0], vt);
        exist_any_record(element(args[1], 1), element(args[1], 2));
        return rvt;
    }
    if (is_node(callee, "remote", 4)) {
        return remote_call({anno, callee}, args, vt);
    }
    if (is_node(callee, "atom", 3)) {
        keyword_warning(element(callee, 1), element(callee, 2));
        const auto asvt = vtupd_export_expr_list(where(atom("call"_atom), anno), args, vt);
        check_call(anno, element(callee, 2), args, element(callee, 1));
        return asvt;
    }
    warn_invalid_call({anno, callee});
    std::vector<Term> parts{callee};
    parts.insert(parts.end(), args.begin(), args.end());
    return vtupd_export_expr_list(where(atom("call"_atom), anno), parts, vt);
}

VarTable Lint::remote_call(const AnnoTerm &call, const std::vector<Term> &args, const VarTable &vt) {
    const auto &anno = call.anno;
    const auto module = element(call.term, 2);
    const auto function = element(call.term, 3);
    if (is_node(module, "atom", 3) && is_node(function, "atom", 3)) {
        const auto m = element(module, 2);
        const auto f = element(function, 2);
        keyword_warning(element(function, 1), f);
        check_remote_function(anno, {m, f}, args);
        check_module_name({anno, m});
        check_remote_self_call(anno, {m, f}, static_cast<std::int64_t>(args.size()));
        return vtupd_export_expr_list(where(atom("call"_atom), anno), args, vt);
    }
    keyword_warning(anno, module);
    keyword_warning(anno, function);
    if (is_node(module, "atom", 3)) {
        check_module_name({element(module, 1), element(module, 2)});
    }
    std::vector<Term> parts{module, function};
    parts.insert(parts.end(), args.begin(), args.end());
    return vtupd_export_expr_list(where(atom("call"_atom), anno), parts, vt);
}

// is_valid_call/1: a literal that cannot be a function is warned about.
void Lint::warn_invalid_call(const AnnoTerm &call) {
    const auto &callee = call.term;
    const auto tag = forms::tag_of(callee);
    const auto size = size_of(callee);
    const auto invalid = (size == 3 && forms::is_one_of(tag, {"char", "integer", "float", "string"})) ||
                         (size == 4 && forms::is_one_of(tag, {"cons", "lc", "record_index"})) ||
                         (size == 2 && tag == "nil") ||
                         (size == 3 && tag == "tuple" && forms::elements(element(callee, 2)).size() != 2);
    if (invalid) {
        add_warning({call.anno, atom("invalid_call"_atom)});
    }
}

VarTable Lint::expr_try(const Term &node, const VarTable &vt) {
    const auto try_where = pair(atom("try"_atom), element(node, 1));
    const auto place = where(atom("try"_atom), element(node, 1));
    const auto evt0 = exprs(element(node, 2), vt);
    const auto uvt = vt_mark_new(VarKind::unsafe, place, evt0, vt);
    const auto inside = vtupdate(evt0, vt);
    auto tables = icrt_clause_tables(element(node, 3), inside);
    const auto caught = catch_clause_tables(element(node, 4), vtupdate(uvt, inside));
    tables.insert(tables.end(), caught.begin(), caught.end());
    const auto sccs = icrt_export(tables, inside, try_where);
    const auto evt1 = vtupdate(uvt, evt0);
    const auto rvt1 = vtupd_unsafe(place, sccs, vt);
    const auto evt2 = vtmerge(evt1, rvt1);
    const auto avt0 = exprs(element(node, 5), vtupdate(evt2, vt));
    const auto avt1 = vtupd_unsafe(place, avt0, vt);
    return vtmerge(evt2, avt1);
}

VarTable Lint::expr_catch(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto evt = expr(element(node, 2), vt);
    if (warn_enabled("deprecated_catch")) {
        add_warning({anno, atom("deprecated_catch"_atom)});
    }
    return vtupd_unsafe(where(atom("catch"_atom), anno), evt, vt);
}

// {match, A, P, E} and {maybe_match, A, P, E}.
VarTable Lint::expr_match(const Term &node, const VarTable &vt) {
    const auto evt = expr(element(node, 3), vt);
    const auto updated = vtupdate(evt, vt);
    const auto pattern = this->pattern(element(node, 2), updated, updated);
    return vtupdate(pattern.fresh, vtmerge(evt, pattern.vt));
}

VarTable Lint::expr_maybe(const Term &node, const VarTable &vt) {
    const auto evt0 = exprs(element(node, 2), vt);
    const auto evt1 = vtupdate(vt_mark_new(VarKind::unsafe, where(atom("maybe"_atom), element(node, 1)), evt0, vt), vt);
    return vtmerge(evt0, evt1);
}

VarTable Lint::expr_maybe_else(const Term &node, const VarTable &vt) {
    const auto otherwise = element(node, 3);
    if (!is_node(otherwise, "else", 3)) {
        lists::no_clause();
    }
    const auto evt0 = exprs(element(node, 2), vt);
    const auto evt1 = vtupdate(vt_mark_new(VarKind::unsafe, where(atom("maybe"_atom), element(node, 1)), evt0, vt), vt);
    const auto else_anno = element(otherwise, 1);
    const auto cvt0 = icrt_clauses(element(otherwise, 2), pair(atom("else"_atom), else_anno), evt1);
    const auto cvt1 = vtupdate(vt_mark_new(VarKind::unsafe, where(atom("else"_atom), else_anno), cvt0, vt), vt);
    return vtmerge(vtmerge(evt0, evt1), vtmerge(cvt0, cvt1));
}

VarTable Lint::expr_unary(const Term &node, const VarTable &vt) {
    const std::vector operand{element(node, 3)};
    return vtupd_export_expr_list(where(element(node, 2), element(node, 1)), operand, vt);
}

VarTable Lint::expr_binary_op(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto op = element(node, 2);
    const auto place = where(op, anno);
    if (is_atom(op, "orelse") || is_atom(op, "andalso")) {
        const std::vector left{element(node, 3)};
        const auto evt1 = vtupd_export_expr_list(place, left, vt);
        const auto vt1 = vtupdate(evt1, vt);
        const auto evt2 = expr(element(node, 4), vt1);
        return vtmerge(evt1, vtupd_unsafe(place, evt2, vt1));
    }
    if (is_atom(op, "=:=") || is_atom(op, "=/=")) {
        expr_check_match_zero(element(node, 3));
        expr_check_match_zero(element(node, 4));
    } else {
        warn_obsolete_op({anno, op});
    }
    const std::vector parts{element(node, 3), element(node, 4)};
    return vtupd_export_expr_list(place, parts, vt);
}

VarTable Lint::expr_remote(const Term &node, const VarTable & /*vt*/) {
    add_error({abstract::first_anno(build_.context(), element(node, 2)), atom("illegal_expr"_atom)});
    return {};
}

// A call of an imported function is checked as a remote call and uses the import; false for any other call.
bool Lint::check_imported_call(const AnnoTerm &call, const std::vector<Term> &args, const Term &key) {
    const auto found = imports_.find(key);
    if (found == imports_.end()) {
        return false;
    }
    check_remote_function(call.anno, {found->second, call.term}, args);
    usage_.imported.insert(pair(key, found->second));
    return true;
}

// A call of an auto-imported BIF: a deprecated one ends the check, a local function of the same name warns.
bool Lint::check_bif_call(const AnnoTerm &call, const Term &key, std::int64_t arity) {
    const auto &name = call.term;
    const auto bif = name.is_atom() && is_bif(name.atom_spelling_unchecked(), arity) && !autoimport_suppressed(key);
    if (!bif) {
        return false;
    }
    if (!is_local_function(key)) {
        deprecated_function(call.anno, {atom("erlang"_atom), name}, arity);
        return true;
    }
    if (warn_enabled("bif_clash") && !bif_clash_disabled(key)) {
        add_warning({call.anno, pair(atom("call_to_redefined_bif"_atom), key)});
    }
    return false;
}

void Lint::check_call(const Term &anno, const Term &name, const std::vector<Term> &args, const Term &name_anno) {
    if (is_atom(name, "record_info") && args.size() == 2) {
        check_record_info_call(anno, args);
        return;
    }
    static_cast<void>(name_anno);
    const auto arity = static_cast<std::int64_t>(args.size());
    const auto key = fa(name, arity);
    if (check_imported_call({anno, name}, args, key)) {
        return;
    }
    if (check_bif_call({anno, name}, key, arity)) {
        return;
    }
    if (!func_ || !collections::same(*func_, key)) {
        call_function({anno, name}, arity);
    }
}

void Lint::check_remote_function(const Term &anno, const Remote &function, const std::vector<Term> &args) {
    const auto arity = static_cast<std::int64_t>(args.size());
    deprecated_function(anno, function, arity);
    if (is_remote(function, "qlc", "q") && arity < 3 && !xqlc_ && !args.empty() && is_node(args[0], "lc", 4)) {
        add_warning({anno, pair(atom("missing_qlc_hrl"_atom), build_.integer(arity))});
    }
    if (is_remote(function, "erlang", "load_nif") && arity == 2) {
        check_load_nif(anno);
    }
    format_function(anno, function, args);
}

// erlang:load_nif/2 is called: the module loads nifs, which an inline option would not survive.
void Lint::check_load_nif(const Term &anno) {
    load_nif_ = true;
    if (warn_enabled("nif_inline") && std::ranges::any_of(compile_list_, is_inline_option)) {
        add_warning({anno, atom("nif_inline"_atom)});
    }
}

// The set of functions option-listed as not to warn for, by the kind of obsolescence.
const TermSet &Lint::obsolete_filter(std::string_view kind) const {
    if (kind == "deprecated") {
        return not_deprecated_;
    }
    return kind == "removed" ? not_removed_ : not_unsafe_;
}

void Lint::deprecated_function(const Term &anno, const Remote &function, std::int64_t arity) {
    const auto info = obsolete(*this, function.module, function.name, arity);
    if (!info) {
        return;
    }
    const auto mfa = triple(function.module, function.name, build_.integer(arity));
    const auto kind = element(*info, 0).atom_spelling_unchecked();
    const auto three = info->tuple_size_unchecked() == 3;
    const auto &filter = obsolete_filter(kind);
    const auto warning = three ? build_.tuple(std::array{element(*info, 0), mfa, element(*info, 1), element(*info, 2)})
                               : triple(element(*info, 0), mfa, element(*info, 1));
    if (warn_enabled(obsolete_flag(kind, *info)) && !filter.contains(function.module) && !filter.contains(mfa)) {
        add_warning({anno, warning});
    }
}

void Lint::check_remote_self_call(const Term &anno, const Remote &function, std::int64_t arity) {
    if (function.module.word() != module_.word() || !function.module.is_atom()) {
        return;
    }
    const auto key = fa(function.name, arity);
    if (warn_enabled("unexported_function") && !export_all() && !exports_.contains(key)) {
        remote_self_calls_[key].insert(anno);
    }
}

VarTable Lint::fun_clauses(const Term &clauses, const VarTable &vt) {
    if (!fun_used_vars_) {
        return fun_clauses1(clauses, vt);
    }
    auto outer = std::move(*fun_used_vars_);
    fun_used_vars_.emplace();
    const auto uvt = fun_clauses1(clauses, vt);
    std::vector<Term> used;
    for (const auto &entry : uvt) {
        if (entry.info.used) {
            used.push_back(entry.name);
        }
    }
    std::vector<std::pair<Term, Term>> entries;
    if (fun_used_vars_) {
        entries.assign(fun_used_vars_->begin(), fun_used_vars_->end());
    }
    const auto inner = need(TermFactory(build_.context()).map(entries));
    outer.insert_or_assign(clauses, pair(build_.list(used), inner));
    fun_used_vars_ = std::move(outer);
    return uvt;
}

VarTable Lint::fun_clauses1(const Term &clauses, const VarTable &vt) {
    const auto outer = recdef_top_;
    recdef_top_ = false;
    VarTable bvt;
    for (const auto &clause : forms::elements(clauses)) {
        bvt = vtmerge(fun_clause(clause, vt), bvt);
    }
    recdef_top_ = outer;
    return vt_no_unsafe(vt_no_unused(vtold(bvt, OldVars{vt})));
}

VarTable Lint::fun_clause(const Term &clause, const VarTable &vt0) {
    if (!is_node(clause, "clause", 5)) {
        lists::no_clause();
    }
    const auto head = this->head(element(clause, 2), vt0, {});
    const auto vt1 = vtupdate(head.vt, vt0);
    shadow_vars(head.fresh, vt0, atom("fun"_atom));
    const auto vt2 = vtupdate(vtnew(vt1, OldVars{head.fresh}), head.fresh);
    const auto gvt = guard(element(clause, 3), vt2);
    const auto vt3 = vtupdate(gvt, vt2);
    const auto bvt = exprs(element(clause, 4), vt3);
    const auto cvt = vtupdate(bvt, vt3);
    static_cast<void>(check_unused_vars(cvt, vt0));
    const auto svt = vtold(vt1, OldVars{head.fresh});
    static_cast<void>(check_old_unused_vars(cvt, svt));
    const auto vt4 = vtmerge(svt, vtnew(cvt, OldVars{svt}));
    return vtold(vt4, OldVars{vt0});
}

VarTable Lint::icrt_clauses(const Term &clauses, const Term &in, const VarTable &vt) {
    return icrt_export(icrt_clause_tables(clauses, vt), vt, in);
}

std::vector<VarTable> Lint::icrt_clause_tables(const Term &clauses, const VarTable &vt0) {
    std::vector<VarTable> tables;
    for (const auto &clause : forms::elements(clauses)) {
        if (!is_node(clause, "clause", 5)) {
            lists::no_clause();
        }
        const auto head = this->head(element(clause, 2), vt0, vt0);
        const auto vt1 = vtupdate(head.vt, head.fresh);
        const auto gvt = guard(element(clause, 3), vtupdate(vt1, vt0));
        const auto vt2 = vtupdate(gvt, vt1);
        const auto bvt = exprs(element(clause, 4), vtupdate(vt2, vt0));
        tables.push_back(vtupdate(bvt, vt2));
    }
    return tables;
}

std::vector<VarTable> Lint::catch_clause_tables(const Term &clauses, const VarTable &vt0) {
    std::vector<VarTable> tables;
    for (const auto &clause : forms::elements(clauses)) {
        const auto patterns = is_node(clause, "clause", 5) ? forms::elements(element(clause, 2)) : std::vector<Term>{};
        if (patterns.size() != 1 || !is_node(patterns[0], "tuple", 3) ||
            forms::elements(element(patterns[0], 2)).size() != 3) {
            lists::no_clause();
        }
        const auto stack = forms::elements(element(patterns[0], 2))[2];
        const auto head = this->head(element(clause, 2), vt0, vt0);
        const auto vt1 = vtupdate(head.vt, head.fresh);
        const auto guard_vt = taint_stack_var(stack, vtupdate(vt1, vt0));
        const auto gvt = guard(element(clause, 3), guard_vt);
        const auto vt2 = vtupdate(gvt, vt1);
        const auto bvt = exprs(element(clause, 4), vtupdate(vt2, vt0));
        tables.push_back(vtupdate(bvt, vt2));
    }
    return tables;
}

// taint_stack_var/3: the stack trace variable must not be used yet; it becomes a guard-only stacktrace variable.
VarTable Lint::taint_stack_var(const Term &stack, const VarTable &vt) {
    if (!is_node(stack, "var", 3) || is_atom(element(stack, 2), "_")) {
        return vt;
    }
    const auto anno = element(stack, 1);
    const auto name = element(stack, 2);
    if (const auto *found = vtfind(vt, name); found != nullptr && found->used) {
        add_error({anno, pair(atom("stacktrace_bound"_atom), name)});
    }
    const VarTable stacktrace{{.name = name, .info = var_info(VarKind::stacktrace, Term{}, false, {anno})}};
    return vtupdate(stacktrace, vt);
}

VarTable Lint::check_unused_vars(const VarTable &vt, const VarTable &old) {
    return warn_unused_vars(UnusedVars{vtnew(unused_vars(vt), OldVars{old})}, vt);
}

VarTable Lint::check_old_unused_vars(const VarTable &vt, const VarTable &old) {
    return warn_unused_vars(UnusedVars{unused_vars(vtold(vt, OldVars{old}))}, vt);
}

VarTable Lint::unused_vars(const VarTable &vt) {
    VarTable out;
    std::ranges::copy_if(vt, std::back_inserter(out),
                         [](const VarEntry &entry) { return !entry.info.used && !underscored(entry.name); });
    return out;
}

VarTable Lint::warn_unused_vars(UnusedVars unused_table, const VarTable &vt) {
    const auto &unused = unused_table.table;
    if (unused.empty()) {
        return vt;
    }
    if (warn_enabled("unused_vars")) {
        for (const auto &entry : unused) {
            for (const auto &anno : entry.info.annos) {
                add_warning({anno, pair(atom("unused_var"_atom), entry.name)});
            }
        }
    }
    auto used = unused;
    for (auto &entry : used) {
        entry.info.used = true;
    }
    return vtmerge(vt, used);
}

void Lint::shadow_vars(const VarTable &vt, const VarTable &old, const Term &in) {
    if (!warn_enabled("shadow_vars")) {
        return;
    }
    for (const auto &entry : vtold(vt, OldVars{vt_no_unsafe(old)})) {
        if (!entry.info.annos.empty()) {
            add_warning({entry.info.annos.front(), triple(atom("shadowed_var"_atom), entry.name, in)});
        }
    }
}

VarTable Lint::expr_var(const Term &name, const Term &anno, const VarTable &vt) {
    if (bvt_) {
        auto found = pat_binsize_var(name, anno, vt, *bvt_);
        bvt_ = vtmerge(*bvt_, found.fresh);
        return found.vt;
    }
    const auto *info = vtfind(vt, name);
    const auto used = [&](const std::vector<Term> &annos) {
        return VarTable{{.name = name, .info = var_info(VarKind::bound, Term{}, true, annos)}};
    };
    if (info == nullptr) {
        unbound_var(name, anno, vt);
        return used({anno});
    }
    switch (info->kind) {
    case VarKind::bound:
        return used(info->annos);
    case VarKind::unsafe:
        add_error({anno, triple(atom("unsafe_var"_atom), name, info->where)});
        return used(info->annos);
    case VarKind::stacktrace:
        add_error({anno, pair(atom("stacktrace_guard"_atom), name)});
        return used(info->annos);
    default:
        return exported_var(name, anno, *info);
    }
}

// A use of an exported variable outside a pattern.
VarTable Lint::exported_var(const Term &name, const Term &anno, const VarInfo &info) {
    if (export_var_subexpr(info.where)) {
        maybe_add_warning(anno, triple(atom("export_var_subexpr"_atom), name, info.where));
    } else if (warn_enabled("export_vars")) {
        add_warning({anno, triple(atom("exported_var"_atom), name, info.where)});
    } else {
        return VarTable{{.name = name,
                         .info = {.kind = VarKind::exported, .where = info.where, .used = true, .annos = info.annos}}};
    }
    return VarTable{{.name = name, .info = var_info(VarKind::bound, Term{}, true, info.annos)}};
}

// unbound_var with a suggestion from the variables in scope.
void Lint::unbound_var(const Term &name, const Term &anno, const VarTable &vt) {
    std::vector<Term> names;
    for (const auto &entry : vt) {
        names.push_back(entry.name);
    }
    const auto guess = most_possible(name, names);
    add_error({anno, guess ? triple(atom("unbound_var"_atom), name, *guess) : pair(atom("unbound_var"_atom), name)});
}

// export_var_subexpr/1: whether the construct is not a block-like one.
bool Lint::export_var_subexpr(const Term &place) const {
    const auto tag = element(place, 0);
    return !(is_atom(tag, "begin") || is_atom(tag, "if") || is_atom(tag, "case") || is_atom(tag, "receive") ||
             is_atom(tag, "try") || is_atom(tag, "maybe"));
}

void Lint::warn_underscore_match(const Term &name, const Term &anno) {
    if (warn_enabled("underscore_match") && underscored(name)) {
        add_warning({anno, pair(atom("match_underscore_var"_atom), name)});
    }
}
} // namespace clause::runtime::builtins::front_end::lint
