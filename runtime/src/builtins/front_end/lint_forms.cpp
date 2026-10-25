#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>
#include <array>
#include <ranges>

// erl_lint's walk over forms (plan 21 step LIB34C): forms/2's preparation (file annotations, local functions,
// auto-import suppression, deprecation filters), the start, attribute and function states, the attributes they
// record, record definitions, function definitions and their clauses.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

std::int64_t length_of(const Term &list) { return static_cast<std::int64_t>(forms::elements(list).size()); }

// The basename of a file name string.
bool is_qlc_header(const Term &file) {
    if (!file.is_list()) {
        return false;
    }
    std::u32string text;
    for (auto rest = file; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto code = string::small_of(rest.head_unchecked());
        text.push_back(code ? static_cast<char32_t>(*code) : U'\0');
    }
    const auto slash = text.find_last_of(U"/\\");
    return text.substr(slash == std::u32string::npos ? 0 : slash + 1) == U"qlc.hrl";
}

// The file a -file attribute form names.
std::optional<Term> file_of(const Term &form) {
    const auto value = attribute(form, "file");
    if (value && is_pair(*value)) {
        return element(*value, 0);
    }
    return std::nullopt;
}

// The text of an attribute's name, empty when it is not an atom.
std::string_view attribute_tag(const Term &form) {
    const auto name = element(form, 2);
    return name.is_atom() ? name.atom_spelling_unchecked() : std::string_view{};
}

// A not_deprecated/2 style filter: the compile option, the set it fills, and whether any pair names a module.
struct Filter {
    std::string_view option;
    TermSet Lint::*set;
    bool any_pair;
};

// The module a nowarn option in a compile attribute lists is checked for being a module name.
void check_listed_module(Lint &lint, const AnnoTerm &listed, bool any_pair) {
    const auto mfa = is_triple(listed.term);
    if (mfa && (any_pair || element(listed.term, 0).is_atom())) {
        lint.check_module_name({listed.anno, element(listed.term, 0)});
    } else if (any_pair && !mfa) {
        lint.check_module_name(listed);
    }
}

// The filter's option in one compile attribute form.
void check_filter_form(Lint &lint, const Filter &filter, const Term &form) {
    const auto value = attribute(form, "compile");
    if (!value) {
        return;
    }
    for (const auto &option : flatten(*value)) {
        if (!is_option_pair(option, filter.option)) {
            continue;
        }
        for (const auto &listed : flatten(element(option, 1))) {
            check_listed_module(lint, {element(form, 1), listed}, filter.any_pair);
        }
    }
}

// What the filter's compile options of the module list go into its set.
void collect_filter_options(Lint &lint, const Filter &filter) {
    for (const auto &option : lint.compile_list_) {
        if (!is_option_pair(option, filter.option)) {
            continue;
        }
        for (const auto &listed : flatten(element(option, 1))) {
            (lint.*filter.set).insert(listed);
        }
    }
}

// The attribute handlers that take the attribute's annotation and value.
using AttributeHandler = void (Lint::*)(const AnnoTerm &);

struct NamedHandler {
    std::string_view tag;
    AttributeHandler handler;
};

constexpr std::array ATTRIBUTE_HANDLERS{
    NamedHandler{.tag = "export", .handler = &Lint::export_attr},
    NamedHandler{.tag = "export_type", .handler = &Lint::export_type},
    NamedHandler{.tag = "import", .handler = &Lint::import},
    NamedHandler{.tag = "optional_callbacks", .handler = &Lint::optional_callbacks},
    NamedHandler{.tag = "on_load", .handler = &Lint::on_load},
    NamedHandler{.tag = "module", .handler = &Lint::module_attr},
    NamedHandler{.tag = "behaviour", .handler = &Lint::behaviour_attr},
    NamedHandler{.tag = "behavior", .handler = &Lint::behaviour_attr},
};

AttributeHandler attribute_handler(std::string_view tag) {
    const auto found = std::ranges::find(ATTRIBUTE_HANDLERS, tag, &NamedHandler::tag);
    return found == ATTRIBUTE_HANDLERS.end() ? nullptr : found->handler;
}

bool is_record_attribute(std::string_view tag) { return tag == "record" || tag == "native_record"; }

bool is_type_attribute(std::string_view tag) { return tag == "type" || tag == "opaque" || tag == "nominal"; }

bool is_doc_attribute(std::string_view tag) { return tag == "doc" || tag == "moduledoc"; }
} // namespace

void Lint::module(const Term &forms_term, const TermMap<BehaviourInfo> &behaviours) {
    forms(forms_term);
    post_traversal_check(forms_, behaviours);
}

// eval_file_attribute/2: every attribute and function annotation gets the file it comes from.
std::vector<Term> Lint::forms_with_files(const Term &forms_term) const {
    std::vector<Term> all;
    auto file = file_;
    for (const auto &form : forms::elements(forms_term)) {
        if (const auto named = file_of(form)) {
            file = *named;
            all.push_back(form);
        } else if (is_node(form, "attribute", 4) || is_node(form, "function", 5)) {
            all.push_back(forms::with_element(build_, form, 1, set_file(element(form, 1), file)));
        } else {
            all.push_back(form);
        }
    }
    return all;
}

// auto_import_suppressed/1.
void Lint::read_no_auto_import() {
    for (const auto &option : compile_list_) {
        no_auto_all_ = no_auto_all_ || is_atom(option, "no_auto_import");
        if (!is_option_pair(option, "no_auto_import")) {
            continue;
        }
        for (const auto &key : flatten(element(option, 1))) {
            if (is_pair(key) && element(key, 0).is_atom() && element(key, 1).is_integer()) {
                no_auto_.insert(key);
            }
        }
    }
}

// The export_all warning of each compile attribute that lists it.
void Lint::warn_export_all(const std::vector<Term> &all) {
    for (const auto &form : all) {
        const auto value = attribute(form, "compile");
        if (value && warn_enabled("export_all") &&
            std::ranges::any_of(flatten(*value), [](const Term &option) { return is_atom(option, "export_all"); })) {
            add_warning({element(form, 1), atom("export_all"_atom)});
        }
    }
}

void Lint::forms(const Term &forms_term) {
    const auto all = forms_with_files(forms_term);
    for (const auto &form : all) {
        if (is_node(form, "function", 5)) {
            locals_.insert(pair(element(form, 2), element(form, 3)));
        }
    }
    read_no_auto_import();
    const auto list = build_.list(all);
    disallowed_compile_flags(list);
    xqlc_ = std::ranges::any_of(all, [](const Term &form) {
        const auto named = file_of(form);
        return named && is_qlc_header(*named);
    });
    not_filtered(list);
    warn_export_all(all);
    for (const auto &form : all) {
        this->form(form);
    }
    forms_ = list;
}

// The errors of the nowarn_bif_clash compile attributes: bare ones and ones with a function list.
BifClashErrors Lint::bif_clash_errors(const Term &forms_term) {
    BifClashErrors errors;
    for (const auto &form : forms::elements(forms_term)) {
        const auto value = attribute(form, "compile");
        if (!value) {
            continue;
        }
        const auto error =
            triple(loc(element(form, 1)).second, atom("erl_lint"_atom), atom("disallowed_nowarn_bif_clash"_atom));
        if (is_atom(*value, "nowarn_bif_clash")) {
            errors.plain.push_back(error);
        } else if (is_option_pair(*value, "nowarn_bif_clash") && is_pair(element(*value, 1))) {
            errors.specific.push_back(error);
        }
    }
    return errors;
}

// disallowed_compile_flags/2: nowarn_bif_clash is an error since R14A.
void Lint::disallowed_compile_flags(const Term &forms_term) {
    const auto errors = bif_clash_errors(forms_term);
    const auto disabled = !warn_enabled("bif_clash");
    if (disabled && errors.plain.empty()) {
        add_error_info(pair(atom("erl_lint"_atom), atom("disallowed_nowarn_bif_clash"_atom)));
        return;
    }
    // Prepended to the error list: the specific ones are older than the plain ones.
    for (const auto &error : std::views::reverse(errors.specific)) {
        add_error_info(error);
    }
    if (disabled) {
        for (const auto &error : std::views::reverse(errors.plain)) {
            add_error_info(error);
        }
    }
}

// not_deprecated/2, not_removed/2 and not_unsafe/2: the functions and modules option-listed as not to warn for.
void Lint::not_filtered(const Term &forms_term) {
    const std::array filters{
        Filter{.option = "nowarn_deprecated_function", .set = &Lint::not_deprecated_, .any_pair = false},
        Filter{.option = "nowarn_removed", .set = &Lint::not_removed_, .any_pair = true},
        Filter{.option = "nowarn_unsafe_function", .set = &Lint::not_unsafe_, .any_pair = false}};
    for (const auto &filter : filters) {
        for (const auto &form : forms::elements(forms_term)) {
            check_filter_form(*this, filter, form);
        }
        collect_filter_options(*this, filter);
    }
}

void Lint::form(const Term &form) {
    if (is_node(form, "error", 2)) {
        add_error_info(element(form, 1));
    } else if (is_node(form, "warning", 2)) {
        add_warning_info(element(form, 1));
    } else if (const auto file = file_of(form)) {
        file_ = *file;
    } else if (attribute(form, "compile")) {
        return;
    } else if (phase_ == Phase::start) {
        start_state(form);
    } else if (phase_ == Phase::attribute) {
        attribute_state(form);
    } else {
        function_state(form);
    }
}

void Lint::start_state(const Term &form) {
    if (const auto name = attribute(form, "module")) {
        const auto anno = element(form, 1);
        phase_ = Phase::attribute;
        if (is_pair(*name)) {
            add_error({anno, atom("pmod_unsupported"_atom)});
            attribute_state(form);
            return;
        }
        module_ = *name;
        check_module_name({anno, *name});
        return;
    }
    const auto anno = element(form, 1);
    add_error({anno, atom("undefined_module"_atom)});
    phase_ = Phase::attribute;
    attribute_state(form);
}

void Lint::attribute_state(const Term &form) {
    if (!is_node(form, "attribute", 4)) {
        phase_ = Phase::function;
        function_state(form);
        return;
    }
    const auto tag = attribute_tag(form);
    const AnnoTerm attribute{element(form, 1), element(form, 3)};
    if (const auto handler = attribute_handler(tag)) {
        (this->*handler)(attribute);
    } else if (tag == "callback" && is_pair(attribute.term)) {
        untrack_doc(false, {}, 0);
        callback_decl(attribute);
    } else if (is_doc_attribute(tag)) {
        track_doc(form);
    } else {
        typed_attribute(form, false);
    }
}

// A module attribute after the first is an error.
void Lint::module_attr(const AnnoTerm &declared) {
    if (!is_atom(module_, "")) {
        add_error({declared.anno, atom("redefine_module"_atom)});
    }
}

void Lint::behaviour_attr(const AnnoTerm &declared) { behaviour_.emplace_back(declared.anno, declared.term); }

// The attributes allowed among the functions too: records, record imports, types and specs.
bool Lint::typed_attribute(const Term &form, bool in_functions) {
    const AnnoTerm attribute{element(form, 1), element(form, 3)};
    const auto tag = attribute_tag(form);
    if (is_record_attribute(tag) && is_pair(attribute.term)) {
        record_def(attribute.anno, tag == "record" ? Flavor::tuple : Flavor::native, attribute.term);
    } else if (tag == "import_record") {
        import_native_record(attribute);
    } else if (is_type_attribute(tag) && is_triple(attribute.term)) {
        untrack_doc(false, {}, 0);
        type_def(element(form, 2), attribute);
    } else if (tag == "spec" && is_pair(attribute.term)) {
        spec_decl(attribute);
    } else if (in_functions) {
        function_attribute(form, tag);
    }
    return true;
}

// An attribute among the functions that is not a type or record one: documentation, or an error.
void Lint::function_attribute(const Term &form, std::string_view tag) {
    if (is_doc_attribute(tag)) {
        track_doc(form);
    } else if (tag != "dialyzer") {
        add_error({element(form, 1), pair(atom("attribute"_atom), element(form, 2))});
    }
}

void Lint::function_state(const Term &form) {
    if (is_node(form, "attribute", 4)) {
        typed_attribute(form, true);
    } else if (is_node(form, "function", 5)) {
        untrack_doc(true, element(form, 2), string::small_of(element(form, 3)).value_or(0));
        function(form);
    } else if (!is_node(form, "eof", 2)) {
        lists::no_clause();
    }
}

void Lint::track_doc(const Term &form) {
    const auto anno = element(form, 1);
    const auto doc = element(form, 3);
    const auto hidden = is_atom(doc, "hidden") || is_atom(doc, "false");
    if (!doc.is_list() && !doc.is_binary() && !hidden) {
        return;
    }
    auto &seen = is_atom(element(form, 2), "moduledoc") ? moduledoc_defined_ : doc_defined_;
    if (seen) {
        const auto location = anno_location(build_.context(), seen->anno);
        const auto line = location.is_tuple() ? element(location, 0) : location;
        add_error({anno, triple(element(form, 2), atom("duplicate_doc_attribute"_atom), line)});
        return;
    }
    seen = DocSeen{.anno = anno, .hidden = hidden};
}

void Lint::untrack_doc(bool function, const Term &name, std::int64_t arity) {
    const auto hidden = (doc_defined_ && doc_defined_->hidden) || (moduledoc_defined_ && moduledoc_defined_->hidden);
    if (function && hidden) {
        hidden_docs_.insert(fa(name, arity));
    }
    doc_defined_.reset();
}

void Lint::export_attr(const AnnoTerm &exported) {
    for (const auto &function : forms::elements(exported.term)) {
        if (exports_.contains(function)) {
            add_warning({exported.anno, pair(atom("duplicated_export"_atom), function)});
        }
        exports_.insert(function);
        called_.emplace_back(function, exported.anno);
    }
}

void Lint::export_type(const AnnoTerm &exported) {
    const auto &list = exported.term;
    const auto valid = list.is_list() && std::ranges::all_of(forms::elements(list), [](const Term &type) {
                           return is_pair(type) && element(type, 0).is_atom() && element(type, 1).is_integer();
                       });
    if (!valid) {
        add_error({exported.anno, pair(atom("bad_export_type"_atom), list)});
        return;
    }
    for (const auto &type : forms::elements(list)) {
        if (exp_types_.contains(type)) {
            add_warning({exported.anno, pair(atom("duplicated_export_type"_atom), type)});
        } else {
            type_id_ = pair(atom("export"_atom), build_.nil());
            used_type(type, exported.anno);
        }
        exp_types_.insert(type);
    }
}

// check_imports/3: the clashes of the functions with earlier imports and BIFs, gathered in reverse.
std::vector<std::pair<Term, std::optional<Term>>> Lint::import_clashes(const TermSet &functions) {
    std::vector<std::pair<Term, std::optional<Term>>> clashes;
    for (const auto &function : functions) {
        if (const auto found = imports_.find(function); found != imports_.end()) {
            clashes.insert(clashes.begin(), {function, found->second});
        } else if (!is_pair(function)) {
            lists::no_clause();
        } else if (is_bif_import(function)) {
            clashes.insert(clashes.begin(), {function, std::nullopt});
        }
    }
    return clashes;
}

// Whether {Name, Arity} names an auto-imported BIF.
bool Lint::is_bif_import(const Term &function) const {
    return element(function, 0).is_atom() &&
           is_bif(element(function, 0).atom_spelling_unchecked(), string::small_of(element(function, 1)).value_or(-1));
}

// Reports the import clashes; true when one is an error.
bool Lint::report_import_clashes(const Term &anno, const std::vector<std::pair<Term, std::optional<Term>>> &clashes) {
    bool failed = false;
    for (const auto &[function, previous] : clashes) {
        if (previous) {
            failed = true;
            add_error({anno, pair(atom("redefine_import"_atom), pair(function, *previous))});
        } else if (warn_enabled("bif_clash") && !bif_clash_disabled(function) && !autoimport_suppressed(function)) {
            add_warning({anno, pair(atom("redefine_bif_import"_atom), function)});
        }
    }
    return failed;
}

void Lint::import(const AnnoTerm &imported) {
    if (!is_pair(imported.term)) {
        lists::no_clause();
    }
    const auto module = element(imported.term, 0);
    check_module_name({imported.anno, module});
    const auto listed = forms::elements(element(imported.term, 1));
    const TermSet functions(listed.begin(), listed.end());
    if (report_import_clashes(imported.anno, import_clashes(functions))) {
        return;
    }
    for (const auto &function : functions) {
        imports_.insert_or_assign(function, module);
    }
}

// The record names an import_record attribute clashes with, newest first: local records and earlier imports.
std::vector<Term> Lint::record_import_clashes(const std::vector<Term> &names) {
    std::vector<Term> clashes;
    for (const auto &name : names) {
        if (records_.contains(name)) {
            clashes.insert(clashes.begin(), name);
        } else if (const auto found = rec_imports_.find(name); found != rec_imports_.end()) {
            clashes.insert(clashes.begin(), pair(found->second, name));
        }
    }
    return clashes;
}

void Lint::import_native_record(const AnnoTerm &imported) {
    if (!is_pair(imported.term)) {
        lists::no_clause();
    }
    const auto module = element(imported.term, 0);
    check_module_name({imported.anno, module});
    const auto names = forms::elements(element(imported.term, 1));
    const auto clashes = record_import_clashes(names);
    if (clashes.empty()) {
        for (const auto &name : names) {
            rec_imports_.insert_or_assign(name, module);
        }
        return;
    }
    for (const auto &clash : clashes) {
        add_error({imported.anno, clash.is_tuple() ? pair(atom("redefine_imported_record"_atom), clash)
                                                   : pair(atom("redefine_local_record"_atom), clash)});
    }
}

void Lint::on_load(const AnnoTerm &declared) {
    const auto &value = declared.term;
    if (!is_pair(value) || !element(value, 0).is_atom() || !element(value, 1).is_integer()) {
        add_error({declared.anno, pair(atom("bad_on_load"_atom), value)});
        return;
    }
    on_load_.insert(on_load_.begin(), value);
    on_load_anno_ = declared.anno;
    if (on_load_.size() > 1) {
        add_error({declared.anno, atom("multiple_on_loads"_atom)});
    } else if (!collections::same(element(value, 1), build_.integer(0))) {
        add_error({declared.anno, pair(atom("bad_on_load_arity"_atom), value)});
    }
}

// normalise_fields/2: a field gets a default, undefined for a tuple record and no_value for a native one.
Term Lint::normalised_field(const Term &field, Flavor flavor) {
    auto plain = is_node(field, "typed_record_field", 3) ? element(field, 1) : field;
    if (is_node(plain, "record_field", 3)) {
        const auto field_anno = element(plain, 1);
        const auto missing = flavor == Flavor::native ? pair(atom("no_value"_atom), field_anno)
                                                      : build_.atom_node(field_anno, "undefined"_atom);
        plain = build_.node("record_field"_atom, field_anno, element(plain, 2), missing);
    }
    return plain;
}

void Lint::record_def(const Term &anno, Flavor flavor, const Term &value) {
    const auto name = element(value, 0);
    if (records_.contains(name)) {
        add_error({anno, pair(atom("redefine_record"_atom), name)});
        return;
    }
    if (const auto found = rec_imports_.find(name); found != rec_imports_.end()) {
        add_error({anno, pair(atom("redefine_imported_record"_atom), pair(found->second, name))});
        return;
    }
    std::vector<Term> normalised;
    std::vector<Term> types;
    for (const auto &field : forms::elements(element(value, 1))) {
        if (is_node(field, "typed_record_field", 3)) {
            types.push_back(element(field, 2));
        }
        normalised.push_back(normalised_field(field, flavor));
    }
    auto fields = def_fields(normalised, flavor, name);
    records_.insert_or_assign(name, RecordDef{.anno = anno, .flavor = flavor, .fields = std::move(fields)});
    type_id_ = pair(atom("record"_atom), name);
    check_type(build_.node("type"_atom, nowarn_anno(), atom("product"_atom), build_.list(types)));
}

std::vector<Term> Lint::def_fields(const std::vector<Term> &fields, Flavor flavor, const Term &name) {
    // Kept newest first, as def_fields/4's fold builds the list.
    std::vector<Term> defined;
    for (const auto &field : fields) {
        if (!is_node(field, "record_field", 4) || !is_node(element(field, 2), "atom", 3)) {
            lists::no_clause();
        }
        const auto field_name = element(element(field, 2), 2);
        const auto exists = std::ranges::any_of(
            defined, [&](const Term &known) { return element(element(known, 2), 2).word() == field_name.word(); });
        if (exists) {
            add_error({element(field, 1), triple(atom("redefine_field"_atom), name, field_name)});
            continue;
        }
        const auto value = element(field, 3);
        if (is_node(value, "no_value", 2)) {
            defined.insert(defined.begin(), field);
        } else if (flavor == Flavor::native) {
            check_field_default(name, field);
            defined.insert(defined.begin(), field);
        } else {
            defined.insert(defined.begin(), checked_default(field));
        }
    }
    return defined;
}

// A tuple record field's default checked as an expression: its errors, warnings and calls kept, and the default
// replaced by undefined when it has errors.
Term Lint::checked_default(const Term &field) {
    const auto before = snapshot();
    recdef_top_ = true;
    static_cast<void>(expr(element(field, 3), {}));
    recdef_top_ = false;
    usage_ = before.usage;
    remote_self_calls_ = before.remote_self_calls;
    load_nif_ = before.load_nif;
    fun_used_vars_ = before.fun_used_vars;
    if (errors_.size() == before.errors) {
        return field;
    }
    const auto atom_node = element(field, 2);
    return forms::with_element(build_, field, 3, build_.atom_node(element(atom_node, 1), "undefined"_atom));
}

void Lint::function(const Term &form) {
    const auto anno = element(form, 1);
    const auto name = element(form, 2);
    const auto arity = string::small_of(element(form, 3)).value_or(0);
    func_ = fa(name, arity);
    define_function(anno, name, arity);
    clauses(element(form, 4));
}

void Lint::define_function(const Term &anno, const Term &name, std::int64_t arity) {
    keyword_warning(anno, name);
    const auto key = fa(name, arity);
    if (defined_.contains(key)) {
        add_error({anno, pair(atom("redefine_function"_atom), key)});
        return;
    }
    constexpr std::int64_t MAX_ARGUMENTS = 255;
    if (arity > MAX_ARGUMENTS) {
        add_error({anno, pair(atom("too_many_arguments"_atom), build_.integer(arity))});
    }
    defined_.insert(key);
    if (is_imported_function(key)) {
        add_error({anno, pair(atom("define_import"_atom), key)});
    }
}

void Lint::clauses(const Term &list) {
    for (const auto &clause : forms::elements(list)) {
        this->clause(clause);
    }
}

void Lint::clause(const Term &clause) {
    if (!is_node(clause, "clause", 5)) {
        lists::no_clause();
    }
    const auto head = this->head(element(clause, 2), {}, {});
    const auto vt1 = vtupdate(head.vt, vtupdate(head.fresh, {}));
    const auto gvt = guard(element(clause, 3), vt1);
    const auto vt2 = vtupdate(gvt, vt1);
    const auto bvt = exprs(element(clause, 4), vt2);
    static_cast<void>(check_unused_vars(vtupdate(bvt, vt2), {}));
}

// nowarn/0: the annotation of the type and product nodes erl_lint makes itself.
Term Lint::nowarn_anno() const {
    const auto generated = anno_set(build_.context(), "generated", atom("true"_atom), build_.integer(0));
    return set_file(generated, build_.nil());
}

// length/1 of a list, for the type and spec checks.
std::int64_t Lint::list_length(const Term &list) const { return length_of(list); }
} // namespace clause::runtime::builtins::front_end::lint
