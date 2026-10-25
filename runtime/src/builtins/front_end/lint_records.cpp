#include "abstract.hpp"
#include "lint.hpp"
#include <algorithm>

// erl_lint's records (plan 21 step LIB34C): check_record/4 with the flavour of each record, field checks of
// constructions, updates, accesses and patterns (tuple and native records), record_info/2 and suggestions.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// The name of a {record_field, _, {atom, _, F}, _} field, none for any other.
std::optional<Term> field_name(const Term &field) {
    if (is_node(field, "record_field", 4) && is_node(element(field, 2), "atom", 3)) {
        return element(element(field, 2), 2);
    }
    return std::nullopt;
}

bool exist_field(const Term &name, const std::vector<Term> &fields) {
    return std::ranges::any_of(fields, [&](const Term &field) {
        const auto found = field_name(field);
        return found && found->word() == name.word();
    });
}

std::optional<Term> find_field(const Term &name, const std::vector<Term> &fields) {
    for (const auto &field : fields) {
        if (const auto found = field_name(field); found && found->word() == name.word()) {
            return element(field, 3);
        }
    }
    return std::nullopt;
}

// has_wildcard_field/1: the annotation of a _ = Value field.
std::optional<Term> wildcard_field(const std::vector<Term> &fields) {
    for (const auto &field : fields) {
        if (is_node(field, "record_field", 4) && is_node(element(field, 2), "var", 3) &&
            is_atom(element(element(field, 2), 2), "_")) {
            return element(element(field, 2), 1);
        }
    }
    return std::nullopt;
}

// is_valid_record/1: what cannot be a record.
bool is_valid_record(const Term &node) {
    const auto tag = forms::tag_of(node);
    const auto size = node.is_tuple() ? node.tuple_size_unchecked() : 0;
    if (size == 3 && forms::is_one_of(tag, {"char", "integer", "float", "atom", "string", "fun"})) {
        return false;
    }
    if (size == 4 && forms::is_one_of(tag, {"cons", "lc", "record_index", "named_fun"})) {
        return false;
    }
    return !(size == 2 && tag == "nil");
}

// Whether the node has `size` elements and one of the tags.
bool is_one_of_nodes(const Term &node, std::size_t size, std::initializer_list<std::string_view> tags) {
    return node.is_tuple() && node.tuple_size_unchecked() == size && forms::is_one_of(forms::tag_of(node), tags);
}

// The name of a field in a native record definition, none for another form.
std::optional<Term> definition_name(const Term &known) {
    if ((is_node(known, "record_field", 4) || is_node(known, "record_field", 3)) &&
        is_node(element(known, 2), "atom", 3)) {
        return element(element(known, 2), 2);
    }
    return std::nullopt;
}

// Whether a default is a constant leaf: a number, atom, string or nil.
bool is_constant_default(const Term &value) {
    return is_one_of_nodes(value, 3, {"integer", "char", "float", "atom", "string"}) || is_node(value, "nil", 2);
}

bool is_literal_update(const Term &node) {
    if (is_node(node, "record", 5) || is_node(node, "map", 4)) {
        return is_literal_update(element(node, 2));
    }
    return is_node(node, "record", 4) || is_node(node, "map", 3);
}
} // namespace

std::optional<RecordView> Lint::check_record(const Term &anno, const Term &name) {
    if (name.is_nil()) {
        return RecordView{.flavor = Flavor::native, .fields = std::nullopt};
    }
    if (is_pair(name)) {
        if (element(name, 0).word() != module_.word()) {
            used_record(element(name, 1));
            return RecordView{.flavor = Flavor::native, .fields = std::nullopt};
        }
        const auto found = records_.find(element(name, 1));
        if (found == records_.end()) {
            suggest_record_name(anno, "undefined_record"_atom, element(name, 1));
            return std::nullopt;
        }
        used_record(element(name, 1));
        return RecordView{.flavor = found->second.flavor, .fields = found->second.fields};
    }
    if (!name.is_atom()) {
        lists::no_clause();
    }
    if (const auto found = records_.find(name); found != records_.end()) {
        used_record(name);
        return RecordView{.flavor = found->second.flavor, .fields = found->second.fields};
    }
    if (rec_imports_.contains(name)) {
        used_record(name);
        return RecordView{.flavor = Flavor::native, .fields = std::nullopt};
    }
    suggest_record_name(anno, "undefined_record"_atom, name);
    return std::nullopt;
}

// record_field access: the source (an expression or guard expression) and the field checked against the record.
VarTable Lint::record_field_expr(const Term &node, const VarTable &vt, bool guard_context) {
    const auto anno = element(node, 1);
    auto rvt = guard_context ? gexpr(element(node, 2), vt) : checked_record_source({anno, element(node, 2)}, vt);
    const auto view = check_record(anno, element(node, 3));
    if (view) {
        record_field(element(node, 4), *view, element(node, 3));
    }
    return vtmerge(rvt, {});
}

// A tuple record built in a guard; a native record cannot be.
VarTable Lint::guard_record(const Term &node, const RecordView &view, const VarTable &vt) {
    if (view.flavor == Flavor::native) {
        add_error({element(node, 1), atom("native_record_in_guard"_atom)});
        return {};
    }
    return ginit_fields(node, *view.fields, vt);
}

VarTable Lint::record_expr(const Term &node, const VarTable &vt, bool guard_context) {
    const auto anno = element(node, 1);
    const auto tag = forms::tag_of(node);
    if (tag == "record_index") {
        record_index(node);
        return {};
    }
    if (tag == "record_field") {
        return record_field_expr(node, vt, guard_context);
    }
    if (node.tuple_size_unchecked() == 5) {
        return record_update(node, vt);
    }
    if (element(node, 2).is_nil() && !guard_context) {
        add_error({anno, pair(atom("undefined_native_record"_atom), atom("_"_atom))});
        return {};
    }
    const auto view = check_record(anno, element(node, 2));
    if (!view) {
        return {};
    }
    return guard_context ? guard_record(node, *view, vt) : init_fields(node, *view, vt);
}

void Lint::record_index(const Term &node) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2);
    const auto view = check_record(anno, name);
    if (!view) {
        return;
    }
    const auto field = element(node, 3);
    if (view->flavor == Flavor::tuple) {
        record_field(field, *view, name);
        return;
    }
    if (!is_node(field, "atom", 3)) {
        lists::no_clause();
    }
    add_error({anno, triple(atom("native_record_illegal_record_index"_atom), name, element(field, 2))});
}

// record_expr/4: a warning for what cannot be a record, then the expression.
VarTable Lint::checked_record_source(const AnnoTerm &source, const VarTable &vt) {
    if (!is_valid_record(source.term)) {
        add_warning({source.anno, atom("invalid_record"_atom)});
    }
    return expr(source.term, vt);
}

// record_field/5: the field must be in the definition (a warning for native records).
void Lint::record_field(const Term &field, const RecordView &view, const Term &name) {
    if (view.flavor == Flavor::native && !view.fields) {
        return;
    }
    if (!is_node(field, "atom", 3)) {
        lists::no_clause();
    }
    const auto field_anno = element(field, 1);
    const auto field_atom = element(field, 2);
    if (find_field(field_atom, *view.fields)) {
        return;
    }
    if (view.flavor == Flavor::native) {
        maybe_add_warning(field_anno, triple(atom("undefined_field"_atom), name, field_atom));
    } else {
        suggest_field_name(field_anno, name, field_atom, *view.fields);
    }
}

VarTable Lint::record_update(const Term &node, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto source = element(node, 2);
    const auto name = element(node, 3);
    const auto rvt = checked_record_source({anno, source}, vt);
    VarTable usvt;
    if (const auto view = check_record(anno, name)) {
        usvt = check_fields({.fields = forms::elements(element(node, 4)),
                             .view = *view,
                             .name = name,
                             .diagnostics = Diagnostics::warning},
                            vt, Checker::expression);
    }
    const auto found = name.is_atom() ? records_.find(name) : records_.end();
    if (found != records_.end() && found->second.flavor == Flavor::tuple) {
        if (const auto wild = wildcard_field(forms::elements(element(node, 4)))) {
            add_error({*wild, pair(atom("wildcard_in_update"_atom), name)});
            return {};
        }
    }
    warn_if_literal_update({anno, source});
    return vtmerge(rvt, usvt);
}

// init_fields/7: the given fields checked, then (unless a native record already failed) the defaults of the others;
// the defaults' usage is kept, and for a native record their errors and warnings too.
VarTable Lint::init_fields(const Term &node, const RecordView &view, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2);
    const auto given = forms::elements(element(node, 3));
    if (view.flavor == Flavor::native && !view.fields) {
        return check_nn_fields(name, given, vt);
    }
    const auto diagnostics = is_pair(name) ? Diagnostics::warning : Diagnostics::error;
    const auto errors = errors_.size();
    const auto vt1 = check_fields({.fields = given, .view = view, .name = name, .diagnostics = diagnostics}, vt,
                                  Checker::expression);
    if (view.flavor == Flavor::native && errors_.size() != errors) {
        return vt1;
    }
    const auto before = snapshot();
    static_cast<void>(check_fields(
        {.fields = default_inits(given, anno, *view.fields), .view = view, .name = name, .diagnostics = diagnostics},
        vt1, Checker::expression));
    called_.resize(before.called);
    remote_self_calls_ = before.remote_self_calls;
    load_nif_ = before.load_nif;
    fun_used_vars_ = before.fun_used_vars;
    if (view.flavor == Flavor::tuple) {
        errors_.resize(before.errors);
        warnings_.resize(before.warnings);
    }
    return vt1;
}

// ginit_fields/6: a tuple record built in a guard; the defaults' errors are kept, their warnings dropped.
VarTable Lint::ginit_fields(const Term &node, const std::vector<Term> &fields, const VarTable &vt) {
    const auto anno = element(node, 1);
    const auto name = element(node, 2);
    const auto given = forms::elements(element(node, 3));
    const RecordView view{.flavor = Flavor::tuple, .fields = fields};
    const auto vt1 = check_fields({.fields = given, .view = view, .name = name, .diagnostics = Diagnostics::error}, vt,
                                  Checker::guard);
    const auto warnings = warnings_.size();
    static_cast<void>(check_fields(
        {.fields = default_inits(given, anno, fields), .view = view, .name = name, .diagnostics = Diagnostics::error},
        vt1, Checker::guard));
    warnings_.resize(warnings);
    return vt1;
}

// init_fields/3: the definition's fields not given, their defaults copied with the construction's annotation.
std::vector<Term> Lint::default_inits(const std::vector<Term> &given, const Term &anno,
                                      const std::vector<Term> &fields) const {
    std::vector<Term> defaults;
    for (const auto &field : fields) {
        const auto found = field_name(field);
        if (found && !exist_field(*found, given)) {
            defaults.push_back(forms::with_element(
                build_, field, 3, abstract::replace_annos(build_.context(), element(field, 3), {anno})));
        }
    }
    return defaults;
}

VarTable Lint::check_fields(const FieldCheck &check, const VarTable &vt, Checker checker) {
    std::vector<Term> seen;
    VarTable merged;
    for (const auto &field : check.fields) {
        merged = vtmerge_pat(merged, check_field(field, check, vt, seen, checker));
    }
    return merged;
}

VarTable Lint::check_value(const Term &value, const VarTable &vt, Checker checker) {
    return checker == Checker::guard ? gexpr(value, vt) : expr(value, vt);
}

VarTable Lint::check_field(const Term &field, const FieldCheck &check, const VarTable &vt, std::vector<Term> &seen,
                           Checker checker) {
    if (!is_node(field, "record_field", 4)) {
        lists::no_clause();
    }
    const auto key = element(field, 2);
    const auto value = element(field, 3);
    if (check.view.flavor == Flavor::native && !check.view.fields) {
        return check_value(value, vt, checker);
    }
    if (!is_node(key, "atom", 3)) {
        return check_variable_field(field, check, vt, seen, checker);
    }
    const auto name = element(key, 2);
    if (std::ranges::any_of(seen, [&](const Term &known) { return known.word() == name.word(); })) {
        add_error({element(field, 1), triple(atom("redefine_field"_atom), check.name, name)});
        return {};
    }
    seen.insert(seen.begin(), name);
    return check_existing_field({element(key, 1), name}, value, check, vt, checker);
}

// A field whose key is a variable: _ initialises the others of a tuple record, any other name is an error.
VarTable Lint::check_variable_field(const Term &field, const FieldCheck &check, const VarTable &vt,
                                    std::vector<Term> &seen, Checker checker) {
    const auto key = element(field, 2);
    if (!is_node(key, "var", 3)) {
        lists::no_clause();
    }
    const auto key_anno = element(key, 1);
    const auto variable = element(key, 2);
    if (is_atom(variable, "_") && check.view.flavor == Flavor::tuple) {
        if (std::ranges::any_of(seen, [](const Term &known) { return is_atom(known, "_"); })) {
            add_error({key_anno, atom("bad_multi_field_init"_atom)});
            return {};
        }
        seen.insert(seen.begin(), variable);
        return check_value(element(field, 3), vt, checker);
    }
    if (is_atom(variable, "_")) {
        add_error({key_anno, atom("native_record_illegal_multi_field_init"_atom)});
        return {};
    }
    add_error({key_anno, triple(atom("field_name_is_variable"_atom), check.name, variable)});
    return vt;
}

// A native field given no value: an error or a warning, by what the check reports.
void Lint::report_novalue(const FieldCheck &check, const AnnoTerm &field) {
    const auto warning = triple(atom("novalue"_atom), check.name, field.term);
    if (check.diagnostics == Diagnostics::error) {
        add_error({field.anno, warning});
    } else if (check.diagnostics == Diagnostics::warning) {
        maybe_add_warning(field.anno, warning);
    }
}

VarTable Lint::check_existing_field(const AnnoTerm &name, const Term &value, const FieldCheck &check,
                                    const VarTable &vt, Checker checker) {
    const auto &fields = *check.view.fields;
    const auto native = check.view.flavor == Flavor::native;
    if (find_field(name.term, fields)) {
        if (native && is_node(value, "no_value", 2)) {
            report_novalue(check, {element(value, 1), name.term});
            return {};
        }
        return check_value(value, vt, checker);
    }
    if (native && check.diagnostics == Diagnostics::warning) {
        maybe_add_warning(name.anno, triple(atom("undefined_field"_atom), check.name, name.term));
    } else {
        suggest_field_name(name.anno, check.name, name.term, fields);
    }
    return {};
}

// check_nn_fields/4: a native record without a definition here, its fields only checked for duplicates.
VarTable Lint::check_nn_fields(const Term &name, const std::vector<Term> &fields, const VarTable &vt) {
    std::vector<Term> seen;
    VarTable merged;
    for (const auto &field : fields) {
        if (!field_name(field)) {
            lists::no_clause();
        }
        const auto field_atom = *field_name(field);
        if (std::ranges::any_of(seen, [&](const Term &known) { return known.word() == field_atom.word(); })) {
            add_error({element(field, 1), triple(atom("redefine_field"_atom), name, field_atom)});
            merged = vtmerge_pat(merged, {});
            continue;
        }
        seen.insert(seen.begin(), field_atom);
        merged = vtmerge_pat(merged, expr(element(field, 3), vt));
    }
    return merged;
}

PatternVars Lint::record_pattern(const Term &node, const VarTable &vt, const VarTable &old) {
    const auto anno = element(node, 1);
    if (is_node(node, "record_index", 4)) {
        record_index(node);
        return {};
    }
    auto name = element(node, 2);
    const auto fields = forms::elements(element(node, 3));
    if (is_pair(name) && element(name, 0).word() == module_.word()) {
        name = element(name, 1);
    } else if (is_pair(name) || name.is_nil()) {
        return native_record_pattern(name, fields, vt, old);
    }
    const auto found = records_.find(name);
    const auto native = found != records_.end() ? found->second.flavor == Flavor::native : rec_imports_.contains(name);
    if (native) {
        used_record(name);
        check_native_record_fields_usage(name, fields);
        return native_record_pattern(name, fields, vt, old);
    }
    if (found == records_.end()) {
        suggest_record_name(anno, "undefined_record"_atom, name);
        return {};
    }
    return tuple_record_pattern({anno, name}, fields, found->second, {.vt = vt, .old = old});
}

// A tuple record pattern: a wildcard field needs every other field given, then the fields are checked.
PatternVars Lint::tuple_record_pattern(const AnnoTerm &name, const std::vector<Term> &fields, const RecordDef &record,
                                       const PatternScope &scope) {
    used_record(name.term);
    if (default_inits(fields, name.anno, record.fields).empty()) {
        if (const auto wild = wildcard_field(fields)) {
            add_error({*wild, atom("bad_multi_field_init"_atom)});
        }
    }
    return pattern_fields(name.term, fields, record, scope);
}

PatternVars Lint::pattern_fields(const Term &name, const std::vector<Term> &fields, const RecordDef &record,
                                 const PatternScope &scope) {
    const RecordView view{.flavor = Flavor::tuple, .fields = record.fields};
    const FieldCheck check{.fields = fields, .view = view, .name = name, .diagnostics = Diagnostics::none};
    std::vector<Term> seen;
    PatternVars all;
    for (const auto &field : fields) {
        const auto found = pattern_field_value(field, check, scope, seen);
        all.vt = vtmerge_pat(all.vt, found.vt);
        all.fresh = found.matched ? vtmerge_pat(all.fresh, found.fresh) : VarTable{};
    }
    return all;
}

// One field of a tuple record pattern: its pattern's variables, or none (then OTP drops the new variables so far).
FieldPattern Lint::pattern_field_value(const Term &field, const FieldCheck &check, const PatternScope &scope,
                                       std::vector<Term> &seen) {
    if (!is_node(field, "record_field", 4)) {
        lists::no_clause();
    }
    const auto key = element(field, 2);
    const auto value = element(field, 3);
    const auto errors = errors_.size();
    const auto warnings = warnings_.size();
    static_cast<void>(errors);
    static_cast<void>(warnings);
    if (is_node(key, "atom", 3)) {
        const auto name = element(key, 2);
        if (std::ranges::any_of(seen, [&](const Term &known) { return known.word() == name.word(); })) {
            add_error({element(field, 1), triple(atom("redefine_field"_atom), check.name, name)});
            return {};
        }
        seen.insert(seen.begin(), name);
        if (!find_field(name, *check.view.fields)) {
            suggest_field_name(element(key, 1), check.name, name, *check.view.fields);
            return {};
        }
        const auto found = pattern(value, scope.vt, scope.old);
        return {.vt = found.vt, .fresh = found.fresh, .matched = true};
    }
    if (!is_node(key, "var", 3)) {
        lists::no_clause();
    }
    if (is_atom(element(key, 2), "_")) {
        if (std::ranges::any_of(seen, [](const Term &known) { return is_atom(known, "_"); })) {
            add_error({element(key, 1), atom("bad_multi_field_init"_atom)});
            return {};
        }
        seen.insert(seen.begin(), element(key, 2));
        const auto found = pattern(value, scope.vt, scope.old);
        return {.vt = found.vt, .fresh = found.fresh, .matched = true};
    }
    add_error({element(key, 1), triple(atom("field_name_is_variable"_atom), check.name, element(key, 2))});
    return {.vt = scope.vt, .fresh = {}, .matched = false};
}

PatternVars Lint::native_record_pattern(const Term &name, const std::vector<Term> &fields, const VarTable &vt,
                                        const VarTable &old) {
    std::vector<Term> seen;
    PatternVars all;
    for (const auto &field : fields) {
        const auto field_atom = field_name(field);
        if (!field_atom) {
            lists::no_clause();
        }
        if (std::ranges::any_of(seen, [&](const Term &known) { return known.word() == field_atom->word(); })) {
            add_error({element(field, 1), triple(atom("redefine_field"_atom), name, *field_atom)});
            all.vt = vtmerge_pat(all.vt, {});
            all.fresh = {};
            continue;
        }
        seen.insert(seen.begin(), *field_atom);
        const auto found = pattern(element(field, 3), vt, old);
        all.vt = vtmerge_pat(all.vt, found.vt);
        all.fresh = vtmerge_pat(all.fresh, found.fresh);
    }
    return all;
}

void Lint::check_native_record_fields_usage(const Term &name, const std::vector<Term> &fields) {
    const auto found = records_.find(name);
    if (found == records_.end() || found->second.flavor != Flavor::native) {
        return;
    }
    for (const auto &field : fields) {
        const auto field_atom = field_name(field);
        if (!field_atom) {
            lists::no_clause();
        }
        const auto defined = std::ranges::any_of(found->second.fields, [&](const Term &known) {
            const auto known_name = definition_name(known);
            return known_name && known_name->word() == field_atom->word();
        });
        if (!defined) {
            maybe_add_warning(element(field, 1), triple(atom("undefined_field"_atom), name, *field_atom));
        }
    }
}

void Lint::exist_any_record(const Term &anno, const Term &name) {
    if (records_.contains(name)) {
        used_record(name);
    } else if (!rec_imports_.contains(name)) {
        suggest_record_name(anno, "undefined_record"_atom, name);
    }
}

void Lint::suggest_record_name(const Term &anno, AtomName error, const Term &name) {
    std::vector<Term> names;
    for (const auto &[record, definition] : records_) {
        names.push_back(record);
    }
    for (const auto &[record, module] : rec_imports_) {
        names.push_back(record);
    }
    const auto guess = name.is_atom() ? most_possible(name, names) : std::nullopt;
    add_error({anno, guess ? triple(atom(error), name, *guess) : pair(atom(error), name)});
}

void Lint::suggest_field_name(const Term &anno, const Term &name, const Term &field, const std::vector<Term> &fields) {
    std::vector<Term> names;
    for (const auto &known : fields) {
        if (!is_node(known, "record_field", 4) || !element(known, 2).is_tuple()) {
            lists::no_clause();
        }
        names.push_back(element(element(known, 2), 2));
    }
    const auto guess = field.is_atom() ? most_possible(field, names) : std::nullopt;
    add_error({anno, guess ? build_.tuple(std::array{atom("undefined_field"_atom), name, field, *guess})
                           : triple(atom("undefined_field"_atom), name, field)});
}

void Lint::check_record_info_call(const Term &anno, const std::vector<Term> &args) {
    if (!is_node(args[0], "atom", 3) || !is_node(args[1], "atom", 3)) {
        add_error({anno, atom("illegal_record_info"_atom)});
        return;
    }
    const auto info_anno = element(args[0], 1);
    const auto info = element(args[0], 2);
    if (!is_atom(info, "fields") && !is_atom(info, "size")) {
        add_error({info_anno, atom("illegal_record_info"_atom)});
        return;
    }
    const auto name = element(args[1], 2);
    const auto found = records_.find(name);
    if (found != records_.end() && found->second.flavor == Flavor::tuple) {
        used_record(name);
    } else {
        add_error({info_anno, atom("native_record_illegal_record_info"_atom)});
    }
}

void Lint::warn_if_literal_update(const AnnoTerm &update) {
    if (is_literal_update(update.term) && warn_enabled("update_literal")) {
        add_warning({update.anno, atom("update_literal"_atom)});
    }
}

void Lint::check_field_default(const Term &name, const Term &field) {
    if (!field_default_ok(element(field, 3))) {
        add_error({element(field, 1),
                   triple(atom("illegal_native_record_default"_atom), name, element(element(field, 2), 2))});
    }
}

// check_field_default_1/1: a constant after partial evaluation.
bool Lint::field_default_ok(const Term &init) const {
    const auto value = partial_eval(init);
    if (is_constant_default(value)) {
        return true;
    }
    if (is_node(value, "cons", 4)) {
        return field_default_ok(element(value, 2)) && field_default_ok(element(value, 3));
    }
    if (is_one_of_nodes(value, 3, {"tuple", "bin"})) {
        return std::ranges::all_of(forms::elements(element(value, 2)),
                                   [&](const Term &item) { return field_default_ok(item); });
    }
    if (is_node(value, "map", 3)) {
        return map_defaults_ok(forms::elements(element(value, 2)));
    }
    return is_node(value, "bin_element", 5) && is_node(element(value, 2), "string", 3);
}

// The keys and values of the map entries are all constants.
bool Lint::map_defaults_ok(const std::vector<Term> &entries) const {
    return std::ranges::all_of(entries, [&](const Term &entry) { return field_default_ok(element(entry, 2)); }) &&
           std::ranges::all_of(entries, [&](const Term &entry) { return field_default_ok(element(entry, 3)); });
}
} // namespace clause::runtime::builtins::front_end::lint
