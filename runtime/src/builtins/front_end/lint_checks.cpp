#include "front_end.hpp"
#include "lint.hpp"
#include <algorithm>
#include <clause/runtime/code_server.hpp>

// erl_lint's checks after the traversal (plan 21 step LIB34C): behaviours, deprecated/removed/unsafe attributes,
// imports, functions (undefined, unused, inline and option lists, on_load, nifs, remote self calls), specs, types,
// records and dialyzer attributes, in post_traversal_check/2's order.
namespace clause::runtime::builtins::front_end::lint {
namespace {
using forms::element;
using forms::is_atom;
using forms::is_node;

// is_fa/1.
bool is_fa(const Term &term) {
    return is_pair(term) && element(term, 0).is_atom() && element(term, 1).is_integer() &&
           string::small_of(element(term, 1)).value_or(0) >= 0;
}

bool is_fa_list(const Term &term) {
    if (!term.is_list()) {
        return false;
    }
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (!is_fa(rest.head_unchecked())) {
            return false;
        }
    }
    return rest.is_nil();
}

// A character list (the flag strings of deprecated, removed and unsafe attributes).
bool is_text(const Term &term) {
    if (!term.is_list()) {
        return false;
    }
    auto rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (!rest.head_unchecked().is_integer()) {
            return false;
        }
    }
    return rest.is_nil();
}

// The listed values of `option` in one compile attribute form, with the form's annotation.
void form_compile_options(const Term &form, std::string_view option, std::vector<std::pair<Term, Term>> &out) {
    const auto value = attribute(form, "compile");
    if (!value) {
        return;
    }
    for (const auto &item : flatten(*value)) {
        if (!is_option_pair(item, option)) {
            continue;
        }
        for (const auto &listed : flatten(element(item, 1))) {
            out.emplace_back(listed, element(form, 1));
        }
    }
}

// The {Option, Value} pairs of compile attributes for one option, with their annotations.
std::vector<std::pair<Term, Term>> compile_options(const Term &forms, std::string_view option) {
    std::vector<std::pair<Term, Term>> out;
    for (const auto &form : forms::elements(forms)) {
        form_compile_options(form, option, out);
    }
    return out;
}

// Whether {Name, Arity} is a function the compiler generates: module_info/0,1 and record_info/2.
bool is_pseudo_function(const Term &function) {
    const auto name = element(function, 0);
    const auto arity = string::small_of(element(function, 1)).value_or(-1);
    return (is_atom(name, "module_info") && arity <= 1) || (is_atom(name, "record_info") && arity == 2);
}

// The defined functions without the generated ones.
std::vector<Term> user_functions(const TermSet &defined) {
    std::vector<Term> out;
    std::ranges::copy_if(defined, std::back_inserter(out),
                         [](const Term &function) { return !is_pseudo_function(function); });
    return out;
}

const std::array<std::string_view, 27> MODULE_DIALYZER_OPTIONS{"no_return",
                                                               "no_unused",
                                                               "no_improper_lists",
                                                               "no_fun_app",
                                                               "no_match",
                                                               "no_opaque",
                                                               "no_fail_call",
                                                               "no_contracts",
                                                               "no_unknown",
                                                               "no_behaviours",
                                                               "no_undefined_callbacks",
                                                               "unmatched_returns",
                                                               "error_handling",
                                                               "race_conditions",
                                                               "no_missing_calls",
                                                               "specdiffs",
                                                               "overspecs",
                                                               "underspecs",
                                                               "unknown",
                                                               "no_underspecs",
                                                               "extra_return",
                                                               "no_extra_return",
                                                               "missing_return",
                                                               "no_missing_return",
                                                               "overlapping_contract",
                                                               "opaque_union",
                                                               "no_opaque_union"};

bool module_dialyzer_option(const Term &option) {
    return option.is_atom() && std::ranges::find(MODULE_DIALYZER_OPTIONS, option.atom_spelling_unchecked()) !=
                                   MODULE_DIALYZER_OPTIONS.end();
}
} // namespace

void Lint::post_traversal_check(const Term &forms, const TermMap<BehaviourInfo> &behaviours) {
    check_behaviour(behaviours);
    check_attribute_functions(forms, "deprecated");
    check_imports(forms);
    check_option_functions(forms, "inline", "bad_inline"_atom);
    check_undefined_functions();
    check_option_functions(forms, "nowarn_unused_function", "bad_nowarn_unused_function"_atom);
    check_unused_functions(forms);
    check_option_functions(forms, "nowarn_bif_clash", "bad_nowarn_bif_clash"_atom);
    check_specs_without_function();
    check_functions_without_spec(forms);
    check_undefined_types();
    check_unused_types(forms);
    check_untyped_records(forms);
    check_on_load();
    check_export_record(forms);
    check_unused_records(forms);
    check_native_records_header(forms);
    check_local_opaque_types();
    check_dialyzer_attribute(forms);
    check_callback_information();
    check_nifs(forms);
    check_unexported_functions();
    check_attribute_functions(forms, "removed");
    check_attribute_functions(forms, "unsafe");
}

namespace {
// The warnings for a behaviour's callbacks the module neither exports nor leaves optional.
void warn_undefined_callbacks(Lint &lint, const BehaviourCallbacks &entry, const TermSet &exported) {
    TermSet required(entry.required.begin(), entry.required.end());
    for (const auto &optional : entry.optional) {
        required.erase(optional);
    }
    for (const auto &callback : required) {
        if (!exported.contains(callback) && is_fa(callback)) {
            lint.maybe_add_warning(entry.anno,
                                   lint.triple(lint.atom("undefined_behaviour_func"_atom), callback, entry.behaviour));
        }
    }
}

// Whether the behaviour leaves the callback optional.
bool is_optional_callback(const BehaviourCallbacks &entry, const Term &callback) {
    return std::ranges::any_of(entry.optional, [&](const Term &known) { return collections::same(known, callback); });
}

// Each callback with the {Anno, Behaviour} pairs that require it.
TermMap<TermSet> behaviour_users(const Lint &lint, const std::vector<BehaviourCallbacks> &all,
                                 const TermSet &exported) {
    TermMap<TermSet> users;
    for (const auto &entry : all) {
        const auto item = lint.pair(entry.anno, entry.behaviour);
        for (const auto &callback : entry.required) {
            if (!is_optional_callback(entry, callback) || exported.contains(callback)) {
                users[callback].insert(item);
            }
        }
    }
    return users;
}

// behaviour_conflicting/2: the warnings for a callback two behaviours both require.
void warn_conflicting(Lint &lint, const Term &callback, const TermSet &items) {
    if (items.size() < 2) {
        return;
    }
    const auto first = *items.begin();
    const auto first_location = lint.loc(element(first, 0)).second;
    for (auto at = std::next(items.begin()); at != items.end(); ++at) {
        lint.maybe_add_warning(element(*at, 0),
                               lint.build_.tuple(std::array{lint.atom("conflicting_behaviours"_atom), callback,
                                                            element(*at, 1), first_location, element(first, 1)}));
    }
}
} // namespace

void Lint::check_behaviour(const TermMap<BehaviourInfo> &behaviours) {
    if (!warn_enabled("behaviours")) {
        return;
    }
    // all_behaviour_callbacks/3: each behaviour's callbacks and optional callbacks.
    std::vector<BehaviourCallbacks> all;
    for (const auto &[anno, behaviour] : behaviour_) {
        all.push_back({.anno = anno, .behaviour = behaviour, .required = {}, .optional = {}});
        behaviour_callbacks(behaviours, all.back());
    }
    const auto exported = exports();
    for (const auto &entry : all) {
        warn_undefined_callbacks(*this, entry, exported);
    }
    for (const auto &[callback, items] : behaviour_users(*this, all, exported)) {
        warn_conflicting(*this, callback, items);
    }
    for (const auto &entry : all) {
        behaviour_deprecated({entry.anno, entry.behaviour}, entry.required, exported);
    }
}

// behaviour_callbacks/3 over the lists erl_lint.erl looked up.
void Lint::behaviour_callbacks(const TermMap<BehaviourInfo> &behaviours, BehaviourCallbacks &out) {
    const auto &anno = out.anno;
    const auto &behaviour = out.behaviour;
    const auto found = behaviours.find(behaviour);
    const auto callbacks = found == behaviours.end() ? atom("error"_atom) : found->second.callbacks;
    if (!is_pair(callbacks)) {
        maybe_add_warning(anno, pair(atom("undefined_behaviour"_atom), behaviour));
        check_module_name({anno, behaviour});
        return;
    }
    const auto value = element(callbacks, 1);
    if (is_atom(value, "undefined")) {
        maybe_add_warning(anno, pair(atom("undefined_behaviour_callbacks"_atom), behaviour));
        return;
    }
    if (!is_fa_list(value)) {
        maybe_add_warning(anno, pair(atom("ill_defined_behaviour_callbacks"_atom), behaviour));
        return;
    }
    out.required = forms::elements(value);
    const auto optional_result = found->second.optional_callbacks;
    if (!is_pair(optional_result) || is_atom(element(optional_result, 1), "undefined")) {
        return;
    }
    if (is_fa_list(element(optional_result, 1))) {
        out.optional = forms::elements(element(optional_result, 1));
    } else {
        maybe_add_warning(anno, pair(atom("ill_defined_optional_callbacks"_atom), behaviour));
    }
}

void Lint::behaviour_deprecated(const AnnoTerm &behaviour, const std::vector<Term> &callbacks,
                                const TermSet &exported) {
    for (const auto &callback : callbacks) {
        if (!exported.contains(callback)) {
            continue;
        }
        const auto arity = string::small_of(element(callback, 1)).value_or(-1);
        const auto info = obsolete_callback(*this, behaviour.term, element(callback, 0), arity);
        if (!info) {
            continue;
        }
        const auto mfa = triple(behaviour.term, element(callback, 0), element(callback, 1));
        if (is_atom(element(*info, 0), "deprecated") && element(*info, 1).is_list()) {
            maybe_add_warning(behaviour.anno, triple(atom("deprecated_callback"_atom), mfa, element(*info, 1)));
        } else if (is_atom(element(*info, 0), "removed")) {
            add_warning({behaviour.anno, triple(atom("removed_callback"_atom), mfa, element(*info, 1))});
        }
    }
}

namespace {
// The error tags of one attribute kind: removed, deprecated or unsafe.
struct ErrorTags {
    AtomName bad;
    AtomName invalid;
};

ErrorTags error_tags(std::string_view attribute_name) {
    if (attribute_name == "removed") {
        return {.bad = "bad_removed"_atom, .invalid = "invalid_removed"_atom};
    }
    if (attribute_name == "deprecated") {
        return {.bad = "bad_deprecated"_atom, .invalid = "invalid_deprecated"_atom};
    }
    return {.bad = "bad_unsafe"_atom, .invalid = "invalid_unsafe"_atom};
}

// Whether a function is exported by the compiler itself and so is not listed in the attributes.
bool is_predefined_export(const Term &function) {
    const auto name = element(function, 0);
    const auto arity = string::small_of(element(function, 1)).value_or(-1);
    return (is_atom(name, "behaviour_info") && arity == 1) || (is_atom(name, "module_info") && arity <= 1);
}

// Whether the flag of a {Function, Flag} item is one the attribute accepts.
bool is_valid_flag(std::string_view attribute_name, const Term &flag) {
    if (attribute_name == "deprecated" && flag.is_atom() &&
        forms::is_one_of(flag.atom_spelling_unchecked(), {"next_version", "next_major_release", "eventually"})) {
        return true;
    }
    return (attribute_name == "unsafe" && is_atom(flag, "possibly")) || is_text(flag);
}

// The errors list when `condition` holds, none otherwise.
std::vector<Term> error_if(bool condition, const Term &error) {
    if (condition) {
        return {error};
    }
    return {};
}

// Whether some exported function has the name.
bool has_exported_name(const std::vector<Term> &exported, const Term &name) {
    return std::ranges::any_of(exported,
                               [&](const Term &function) { return element(function, 0).word() == name.word(); });
}
} // namespace

// check_deprecated/2, check_removed/2 and check_unsafe/2: the functions the attribute lists must be exported (or,
// for removed, must not be).
void Lint::check_attribute_functions(const Term &forms, std::string_view attribute_name) {
    std::vector<Term> exported;
    std::ranges::copy_if(exports(), std::back_inserter(exported),
                         [](const Term &function) { return !is_predefined_export(function); });
    for (const auto &form : forms::elements(forms)) {
        const auto value = attribute(form, attribute_name);
        if (!value) {
            continue;
        }
        for (const auto &item : flatten(*value)) {
            for (const auto &error : attribute_errors(attribute_name, item, exported)) {
                add_error({element(form, 1), error});
            }
        }
    }
}

// depr_cat/3, removed_cat/3 and unsafe_cat/3 for one item.
std::vector<Term> Lint::attribute_errors(std::string_view attribute_name, const Term &item,
                                         const std::vector<Term> &exported) {
    const auto invalid = atom(error_tags(attribute_name).invalid);
    if (is_triple(item)) {
        if (!is_valid_flag(attribute_name, element(item, 2))) {
            return {pair(invalid, item)};
        }
        return attribute_function(attribute_name, element(item, 0), element(item, 1), exported);
    }
    if (is_pair(item)) {
        return attribute_function(attribute_name, element(item, 0), element(item, 1), exported);
    }
    if (!is_atom(item, "module")) {
        return {pair(invalid, item)};
    }
    if (attribute_name != "removed") {
        return {};
    }
    return attribute_function(attribute_name, atom("_"_atom), atom("_"_atom), exported);
}

std::vector<Term> Lint::attribute_function(std::string_view attribute_name, const Term &name, const Term &arity,
                                           const std::vector<Term> &exported) {
    const auto removed = attribute_name == "removed";
    const auto tags = error_tags(attribute_name);
    const auto bad = atom(tags.bad);
    const auto wild = atom("_"_atom);
    if (is_atom(name, "_") && is_atom(arity, "_")) {
        return error_if(removed && !exported.empty(), pair(bad, pair(wild, wild)));
    }
    if (name.is_atom() && is_atom(arity, "_")) {
        return error_if(has_exported_name(exported, name) == removed, pair(bad, pair(name, wild)));
    }
    const auto value = arity.is_integer() ? string::small_of(arity) : std::nullopt;
    if (!name.is_atom() || !value || *value < 0) {
        return {pair(atom(tags.invalid), pair(name, arity))};
    }
    const auto function = pair(name, arity);
    return error_if(is_exported_or_builtin(function, *value, exported) == removed, pair(bad, function));
}

// Whether {Name, Arity} is exported or one of the module's builtins.
bool Lint::is_exported_or_builtin(const Term &function, std::int64_t arity, const std::vector<Term> &exported) const {
    const auto listed =
        std::ranges::any_of(exported, [&](const Term &known) { return collections::same(known, function); });
    return listed || is_builtin(module_, element(function, 0), arity);
}

// erlang:is_builtin/3 over the runtime's builtins.
bool Lint::is_builtin(const Term &module, const Term &name, std::int64_t arity) const {
    if (!module.is_atom()) {
        return false;
    }
    const auto m = module.atom_spelling_unchecked();
    const auto f = name.atom_spelling_unchecked();
    if (m == "erlang" && f == "apply" && (arity == 2 || arity == 3)) {
        return true;
    }
    return build_.context().code_server().builtins().find(m, f, static_cast<std::size_t>(arity)) != nullptr;
}

// The unused_import warnings of one import attribute.
void Lint::warn_unused_imports(const Term &form, const Term &value) {
    const auto listed = forms::elements(element(value, 1));
    const TermSet functions(listed.begin(), listed.end());
    for (const auto &function : functions) {
        const auto key = pair(function, element(value, 0));
        const auto found = imports_.find(function);
        const auto imported = found != imports_.end() && collections::same(found->second, element(value, 0));
        if (imported && !usage_.imported.contains(key)) {
            add_warning({element(form, 1), pair(atom("unused_import"_atom), key)});
        }
    }
}

void Lint::check_imports(const Term &forms) {
    if (!warn_enabled("unused_import")) {
        return;
    }
    for (const auto &form : forms::elements(forms)) {
        const auto value = attribute(form, "import");
        if (value && is_pair(*value)) {
            warn_unused_imports(form, *value);
        }
    }
}

void Lint::check_option_functions(const Term &forms, std::string_view tag, AtomName type) {
    auto defined = user_functions(defined_);
    for (const auto &[function, module] : imports_) {
        defined.push_back(function);
    }
    std::vector<std::pair<Term, Term>> bad;
    for (const auto &[function, anno] : compile_options(forms, tag)) {
        if (std::ranges::none_of(defined, [&](const Term &known) { return collections::same(known, function); })) {
            bad.emplace_back(function, anno);
        }
    }
    func_location_error(type, bad, defined);
}

namespace {
// The same-named functions' arities and the names of the functions with the arity, among the defined ones.
struct Candidates {
    std::vector<Term> arities;
    std::vector<Term> names;
};

Candidates candidates_for(const Term &function, const std::vector<Term> &defined) {
    Candidates found;
    const auto name = element(function, 0);
    for (const auto &known : defined) {
        if (element(known, 0).word() == name.word()) {
            found.arities.push_back(element(known, 1));
        } else if (collections::same(element(known, 1), element(function, 1))) {
            found.names.push_back(element(known, 0));
        }
    }
    std::ranges::sort(found.arities, TermLess{});
    return found;
}
} // namespace

void Lint::func_location_error(AtomName type, const std::vector<std::pair<Term, Term>> &bad,
                               const std::vector<Term> &defined) {
    for (const auto &[function, anno] : bad) {
        if (!is_pair(function)) {
            add_error({anno, pair(atom("invalid_fa_attribute"_atom), function)});
            continue;
        }
        const auto name = element(function, 0);
        const auto found = candidates_for(function, defined);
        if (!found.arities.empty()) {
            add_error({anno, triple(atom(type), function, pair(name, build_.list(found.arities)))});
            continue;
        }
        const auto guess = name.is_atom() ? most_possible(name, found.names) : std::nullopt;
        add_error({anno, guess
                             ? triple(atom(type), function, pair(*guess, build_.list(std::array{element(function, 1)})))
                             : pair(atom(type), function)});
    }
}

namespace {
// The functions reachable from the exports and on_load functions over the calls.
TermSet reachable_functions(const Lint &lint, const TermMap<std::vector<Term>> &calls) {
    TermSet used;
    std::vector<Term> pending(lint.on_load_.begin(), lint.on_load_.end());
    pending.insert(pending.end(), lint.exports_.begin(), lint.exports_.end());
    while (!pending.empty()) {
        const auto function = pending.back();
        pending.pop_back();
        if (!used.insert(function).second) {
            continue;
        }
        if (const auto found = calls.find(function); found != calls.end()) {
            pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
    }
    return used;
}
} // namespace

void Lint::warn_unused_function(const std::vector<Term> &forms, const Term &function) {
    for (const auto &form : forms) {
        if (is_node(form, "function", 5) && collections::same(pair(element(form, 2), element(form, 3)), function)) {
            add_warning({element(form, 1), pair(atom("unused_function"_atom), function)});
        }
    }
}

void Lint::check_unused_functions(const Term &forms) {
    if (export_all() || !warn_enabled("unused_function") || !usage_.calls) {
        return;
    }
    auto used = reachable_functions(*this, *usage_.calls);
    for (const auto &option : compile_list_) {
        if (is_option_pair(option, "nowarn_unused_function")) {
            const auto listed = flatten(element(option, 1));
            used.insert(listed.begin(), listed.end());
        }
    }
    const auto form_list = forms::elements(forms);
    for (const auto &function : defined_) {
        if (!used.contains(function)) {
            warn_unused_function(form_list, function);
        }
    }
}

void Lint::check_undefined_functions() {
    TermSet called;
    for (const auto &[function, anno] : called_) {
        if (!defined_.contains(function)) {
            called.insert(pair(function, anno));
        }
    }
    std::vector<std::pair<Term, Term>> bad;
    for (const auto &entry : called) {
        bad.emplace_back(element(entry, 0), element(entry, 1));
    }
    func_location_error("undefined_function"_atom, bad, std::vector<Term>(defined_.begin(), defined_.end()));
}

void Lint::check_specs_without_function() {
    for (const auto &[mfa, anno] : specs_) {
        if (element(mfa, 0).word() != module_.word()) {
            continue;
        }
        const auto function = pair(element(mfa, 1), element(mfa, 2));
        if (!defined_.contains(function)) {
            add_error({anno, pair(atom("spec_fun_undefined"_atom), function)});
        }
    }
}

namespace {
// The kind of missing_spec warning that is on: all, exported or documented; empty when none is.
std::string_view missing_spec_kind(const Lint &lint) {
    if (lint.warn_enabled("missing_spec_all")) {
        return "all";
    }
    if (lint.warn_enabled("missing_spec")) {
        return "exported";
    }
    return lint.warn_enabled("missing_spec_documented") ? "documented" : std::string_view{};
}

// Whether the missing_spec kind asks a spec of the function.
bool spec_wanted(const Lint &lint, std::string_view kind, const Term &function, const TermSet &exported) {
    if (kind == "all") {
        return true;
    }
    return exported.contains(function) && !is_pseudo_function(function) &&
           (kind != "documented" || !lint.hidden_docs_.contains(function));
}
} // namespace

void Lint::check_functions_without_spec(const Term &forms) {
    const auto kind = missing_spec_kind(*this);
    if (kind.empty()) {
        return;
    }
    TermSet specified;
    for (const auto &[mfa, anno] : specs_) {
        specified.insert(pair(element(mfa, 1), element(mfa, 2)));
    }
    const auto exported = exports();
    for (const auto &form : forms::elements(forms)) {
        if (!is_node(form, "function", 5)) {
            continue;
        }
        const auto function = pair(element(form, 2), element(form, 3));
        if (spec_wanted(*this, kind, function, exported) && !specified.contains(function)) {
            add_warning({element(form, 1), pair(atom("missing_spec"_atom), function)});
        }
    }
}

void Lint::check_undefined_types() {
    for (const auto &[type_pair, uses] : usage_.used_types) {
        const auto name = element(type_pair, 0);
        const auto arity = string::small_of(element(type_pair, 1)).value_or(-1);
        if (types_.contains(type_pair) ||
            (name.is_atom() && is_predefined_type(name.atom_spelling_unchecked(), arity))) {
            continue;
        }
        for (auto at = uses.rbegin(); at != uses.rend(); ++at) {
            add_error({at->anno, pair(atom("undefined_type"_atom), type_pair)});
        }
    }
}

// The first file attribute of the forms.
std::optional<Term> Lint::first_file(const Term &forms) const {
    for (const auto &form : forms::elements(forms)) {
        if (const auto value = attribute(form, "file"); value && is_pair(*value)) {
            return element(*value, 0);
        }
    }
    return std::nullopt;
}

namespace {
// reached_types/1: the uses of types by the places using them, and the places that are roots.
void collect_type_uses(const Lint &lint, TermMap<TermSet> &edges, std::vector<Term> &pending) {
    for (const auto &[type_pair, uses] : lint.usage_.used_types) {
        for (const auto &use : uses) {
            edges[use.at].insert(lint.pair(lint.atom("type"_atom), type_pair));
            if (is_pair(use.at) && !is_atom(element(use.at, 0), "type")) {
                pending.push_back(use.at);
            }
        }
    }
}

// The types reachable from exports, specs and records over the uses of types.
TermSet reached_types(const Lint &lint) {
    TermMap<TermSet> edges;
    std::vector<Term> pending;
    collect_type_uses(lint, edges, pending);
    TermSet reached;
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        if (!reached.insert(node).second) {
            continue;
        }
        if (const auto found = edges.find(node); found != edges.end()) {
            pending.insert(pending.end(), found->second.begin(), found->second.end());
        }
    }
    return reached;
}
} // namespace

void Lint::check_unused_types(const Term &forms) {
    const auto file = first_file(forms);
    if (!warn_enabled("unused_type") || !file) {
        return;
    }
    const auto reached = reached_types(*this);
    for (const auto &[type_pair, info] : types_) {
        if (is_node(element(type_pair, 0), "record", 2)) {
            continue;
        }
        if (collections::same(loc(info.anno).first, *file) && !reached.contains(pair(atom("type"_atom), type_pair))) {
            add_warning({info.anno, pair(atom("unused_type"_atom), type_pair)});
        }
    }
}

namespace {
// The names of the records whose fields all have types.
TermSet typed_record_names(const Term &forms) {
    TermSet typed;
    for (const auto &form : forms::elements(forms)) {
        const auto value = attribute(form, "record");
        if (!value || !is_pair(*value)) {
            continue;
        }
        const auto fields = forms::elements(element(*value, 1));
        if (std::ranges::all_of(fields, [](const Term &field) { return is_node(field, "typed_record_field", 3); })) {
            typed.insert(element(*value, 0));
        }
    }
    return typed;
}
} // namespace

void Lint::check_untyped_records(const Term &forms) {
    if (!warn_enabled("untyped_record")) {
        return;
    }
    const auto typed = typed_record_names(forms);
    for (const auto &[name, record] : records_) {
        if (!typed.contains(name) && !record.fields.empty()) {
            add_warning({record.anno, pair(atom("untyped_record"_atom), name)});
        }
    }
}

void Lint::check_on_load() {
    if (on_load_.size() != 1 || !collections::same(element(on_load_[0], 1), build_.integer(0))) {
        return;
    }
    const auto function = on_load_[0];
    if (defined_.contains(function)) {
        return;
    }
    std::vector<Term> names;
    for (const auto &known : defined_) {
        if (collections::same(element(known, 1), build_.integer(0))) {
            names.push_back(element(known, 0));
        }
    }
    const auto guess = most_possible(element(function, 0), names);
    add_error({on_load_anno_, guess ? triple(atom("undefined_on_load"_atom), function, *guess)
                                    : pair(atom("undefined_on_load"_atom), function)});
}

namespace {
// One name of an export_record attribute: its errors, and whether it counts as seen.
void export_record_name(Lint &lint, const AnnoTerm &exported, TermSet &seen) {
    const auto &name = exported.term;
    if (seen.contains(name)) {
        lint.add_error({exported.anno, lint.pair(lint.atom("native_record_already_exported"_atom), name)});
        return;
    }
    const auto found = lint.records_.find(name);
    if (found == lint.records_.end()) {
        seen.insert(name);
        lint.add_error({exported.anno, lint.pair(lint.atom("undefined_native_record"_atom), name)});
    } else if (found->second.flavor == Flavor::tuple) {
        seen.insert(name);
        lint.used_record(name);
        lint.add_error({exported.anno, lint.atom("tuple_record_export"_atom)});
    } else {
        lint.used_record(name);
    }
}
} // namespace

void Lint::check_export_record(const Term &forms) {
    TermSet seen;
    for (const auto &form : forms::elements(forms)) {
        const auto value = attribute(form, "export_record");
        if (!value) {
            continue;
        }
        const auto anno = element(form, 1);
        auto rest = *value;
        for (; rest.is_cons() && rest.head_unchecked().is_atom(); rest = rest.tail_unchecked()) {
            export_record_name(*this, {anno, rest.head_unchecked()}, seen);
        }
        if (!rest.is_nil()) {
            add_error({anno, atom("bad_export_record"_atom)});
        }
    }
}

void Lint::check_unused_records(const Term &forms) {
    const auto file = first_file(forms);
    if (!warn_enabled("unused_record") || !file) {
        return;
    }
    for (const auto &[record, anno] : compile_options(forms, "nowarn_unused_record")) {
        exist_any_record(anno, record);
    }
    for (const auto &[name, record] : records_) {
        if (!usage_.used_records.contains(name) && collections::same(loc(record.anno).first, *file)) {
            add_warning({record.anno, pair(atom("unused_record"_atom), name)});
        }
    }
}

void Lint::check_native_records_header(const Term &forms) {
    const auto file = first_file(forms);
    if (!warn_enabled("native_record_header") || !file) {
        return;
    }
    for (const auto &[name, record] : records_) {
        if (record.flavor == Flavor::native && !collections::same(loc(record.anno).first, *file)) {
            add_warning({record.anno, pair(atom("native_record_header"_atom), name)});
        }
    }
}

void Lint::check_local_opaque_types() {
    for (const auto &[type_pair, info] : types_) {
        if (is_atom(info.attr, "opaque") && !exp_types_.contains(type_pair)) {
            add_warning({info.anno, pair(atom("not_exported_opaque"_atom), type_pair)});
        }
    }
}

namespace {
// An {Options, Functions} dialyzer item expands to one {Option, Function} per combination.
void expand_dialyzer_item(const Lint &lint, const AnnoTerm &item, std::vector<AnnoTerm> &out) {
    if (!is_pair(item.term)) {
        out.push_back(item);
        return;
    }
    for (const auto &option : flatten(element(item.term, 0))) {
        for (const auto &function : flatten(element(item.term, 1))) {
            out.push_back({item.anno, lint.pair(option, function)});
        }
    }
}

// The values of the dialyzer attributes, each with its attribute's annotation.
std::vector<AnnoTerm> dialyzer_values(const Lint &lint, const Term &forms) {
    std::vector<AnnoTerm> values;
    for (const auto &form : forms::elements(forms)) {
        const auto value = attribute(form, "dialyzer");
        if (!value) {
            continue;
        }
        for (const auto &item : flatten(*value)) {
            expand_dialyzer_item(lint, {element(form, 1), item}, values);
        }
    }
    return values;
}

bool is_wellformed_dialyzer(const Term &value) {
    return (is_pair(value) && element(value, 0).is_atom() && is_fa(element(value, 1))) || value.is_atom();
}

// The error of a well-formed dialyzer value that names an unknown option or an undefined function.
void check_dialyzer_value(Lint &lint, const AnnoTerm &value, const std::vector<Term> &defined) {
    const auto pair_value = is_pair(value.term);
    const auto option = pair_value ? element(value.term, 0) : value.term;
    const auto known = module_dialyzer_option(option) || (pair_value && is_atom(option, "nowarn_function"));
    if (!known) {
        lint.add_error({value.anno, lint.pair(lint.atom("bad_dialyzer_option"_atom), option)});
        return;
    }
    if (pair_value && std::ranges::none_of(defined, [&](const Term &function) {
            return collections::same(function, element(value.term, 1));
        })) {
        lint.func_location_error("undefined_function"_atom, {{element(value.term, 1), value.anno}}, defined);
    }
}
} // namespace

void Lint::check_dialyzer_attribute(const Term &forms) {
    const auto values = dialyzer_values(*this, forms);
    for (const auto &value : values) {
        if (!is_wellformed_dialyzer(value.term)) {
            add_error({value.anno, pair(atom("bad_dialyzer_attribute"_atom), value.term)});
        }
    }
    const auto defined = user_functions(defined_);
    for (const auto &value : values) {
        if (is_wellformed_dialyzer(value.term)) {
            check_dialyzer_value(*this, value, defined);
        }
    }
}

void Lint::check_callback_information() {
    for (const auto &[mfa, anno] : optional_callbacks_) {
        if (!callbacks_.contains(mfa)) {
            add_error({anno, pair(atom("undefined_callback"_atom), mfa)});
        }
    }
    if (defined_.contains(fa(atom("behaviour_info"_atom), 1)) && !callbacks_.empty()) {
        for (const auto &[mfa, anno] : callbacks_) {
            add_error({anno, pair(atom("behaviour_info"_atom), mfa)});
        }
    }
}

namespace {
// The {Function, Anno} of each function a nifs attribute lists.
std::vector<std::pair<Term, Term>> nif_attributes(const Term &forms) {
    std::vector<std::pair<Term, Term>> nifs;
    for (const auto &form : forms::elements(forms)) {
        const auto value = attribute(form, "nifs");
        if (!value) {
            continue;
        }
        for (const auto &function : forms::elements(*value)) {
            nifs.emplace_back(function, element(form, 1));
        }
    }
    return nifs;
}
} // namespace

void Lint::check_nifs(const Term &forms) {
    const auto nifs = nif_attributes(forms);
    if (!nifs.empty() && !load_nif_) {
        add_warning({nifs.front().second, atom("no_load_nif"_atom)});
    }
    const auto defined = user_functions(defined_);
    std::vector<std::pair<Term, Term>> bad;
    for (const auto &entry : nifs) {
        if (std::ranges::none_of(defined, [&](const Term &f) { return collections::same(f, entry.first); })) {
            bad.push_back(entry);
        }
    }
    func_location_error("undefined_nif"_atom, bad, defined);
}

void Lint::check_unexported_functions() {
    if (!callbacks_.empty() || !optional_callbacks_.empty()) {
        exports_.insert(fa(atom("behaviour_info"_atom), 1));
    }
    for (const auto &[function, annos] : remote_self_calls_) {
        if (exports_.contains(function)) {
            continue;
        }
        const auto mfa = triple(module_, element(function, 0), element(function, 1));
        for (const auto &anno : annos) {
            add_warning({anno, pair(atom("unexported_function"_atom), mfa)});
        }
    }
}
} // namespace clause::runtime::builtins::front_end::lint
