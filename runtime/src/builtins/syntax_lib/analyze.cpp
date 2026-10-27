#include "syntax_lib.hpp"

// erl_syntax_lib's analyses as builtins (plan 21 LIB77E4a): forms, attributes, function and type names, record
// expressions and fields, applications; each throws syntax_error for a node it cannot analyse.
namespace clause::runtime::builtins::syntax_lib {
namespace {
bool type_is(ProcessContext &context, const Term &node, std::string_view type) {
    return type_of(context, node) == type;
}

// The atom of an atom node; syntax_error for any other node.
Term atom_name(ProcessContext &context, const Term &node) {
    if (!type_is(context, node, "atom")) {
        syntax_error(context);
    }
    return get(context, node, "atom_value");
}

// append_arity/2: Name becomes {Name, Arity} (inside a module qualifier); a name with an arity keeps its own.
Term with_arity(ProcessContext &context, const Term &arity, const Term &name) {
    const Trees trees(context);
    if (name.is_tuple() && name.tuple_size_unchecked() == 2) {
        return trees.tuple(std::array{element(name, 0), with_arity(context, arity, element(name, 1))});
    }
    if (name.is_atom()) {
        return trees.tuple(std::array{name, arity});
    }
    return collections::same(arity, name) ? arity : name;
}

// The arguments of an attribute when they are a list of `count`; syntax_error otherwise, or for another node.
Items attribute_arguments_of(ProcessContext &context, const Term &node, std::size_t count) {
    if (!type_is(context, node, "attribute")) {
        syntax_error(context);
    }
    const auto arguments = attribute_arguments(context, node);
    if (!list_of(arguments, count)) {
        syntax_error(context);
    }
    return syntax::elements(arguments);
}

// The number of arguments of an attribute: 0 for none or anything but a list of one or two.
std::size_t attribute_arity(ProcessContext &context, const Term &node) {
    if (!type_is(context, node, "attribute")) {
        syntax_error(context);
    }
    const auto arguments = attribute_arguments(context, node);
    return list_of(arguments, 1) ? 1 : list_of(arguments, 2) ? 2 : 0;
}

// The elements of a proper list node, each read by `read`; syntax_error for anything else.
template <typename Read> Term list_of_names(ProcessContext &context, const Term &node, Read read) {
    if (!is_proper_list(context, node)) {
        syntax_error(context);
    }
    Items names;
    for (const auto &item : syntax::elements(list_elements(context, node))) {
        names.push_back(read(item));
    }
    return Trees(context).list(names);
}

Term function_names(ProcessContext &context, const Term &node) {
    return list_of_names(context, node, [&](const Term &item) { return function_name_of(context, item); });
}

Term analyze_module_attribute(ProcessContext &context, const Term &node) {
    const auto arity = attribute_arity(context, node);
    if (arity == 0) {
        syntax_error(context);
    }
    const auto arguments = syntax::elements(attribute_arguments(context, node));
    const auto module = atom_name(context, arguments[0]);
    if (arity == 1) {
        return module;
    }
    const auto variables =
        list_of_names(context, arguments[1], [&](const Term &item) { return get(context, item, "variable_name"); });
    return Trees(context).tuple(std::array{module, variables});
}

Term analyze_export_attribute(ProcessContext &context, const Term &node) {
    return function_names(context, attribute_arguments_of(context, node, 1)[0]);
}

Term analyze_import_attribute(ProcessContext &context, const Term &node) {
    const auto arity = attribute_arity(context, node);
    if (arity == 0) {
        syntax_error(context);
    }
    const auto arguments = syntax::elements(attribute_arguments(context, node));
    const auto module = atom_name(context, arguments[0]);
    if (arity == 1) {
        return module;
    }
    return Trees(context).tuple(std::array{module, function_names(context, arguments[1])});
}

Term analyze_file_attribute(ProcessContext &context, const Term &node) {
    const auto arguments = attribute_arguments_of(context, node, 2);
    if (!type_is(context, arguments[0], "string") || !type_is(context, arguments[1], "integer")) {
        syntax_error(context);
    }
    return Trees(context).tuple(
        std::array{get(context, arguments[0], "string_value"), get(context, arguments[1], "integer_value")});
}

// {Name, Value} of an attribute with one argument whose concrete value exists.
Term analyze_wild_attribute(ProcessContext &context, const Term &node) {
    if (!type_is(context, node, "attribute")) {
        syntax_error(context);
    }
    const auto name = atom_name(context, attribute_name(context, node));
    const auto arguments = attribute_arguments(context, node);
    if (!list_of(arguments, 1)) {
        syntax_error(context);
    }
    std::optional<Term> value;
    try {
        value = concrete(context, first(arguments));
    } catch (const BuiltinFailure &failure) {
        if (failure.term) {
            throw;
        }
        syntax_error(context);
    }
    return Trees(context).tuple(std::array{name, *value});
}

Term analyze_record_field(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    const auto type = type_of(context, node);
    if (type == "record_field") {
        const auto name = atom_name(context, get(context, node, "record_field_name"));
        const auto value = get(context, node, "record_field_value");
        return trees.tuple(std::array{name, trees.tuple(std::array{value, trees.atom("none"_atom)})});
    }
    if (type != "typed_record_field") {
        syntax_error(context);
    }
    const auto field = analyze_record_field(context, get(context, node, "typed_record_field_body"));
    const auto value = element(element(field, 1), 0);
    const auto field_type = get(context, node, "typed_record_field_type");
    return trees.tuple(std::array{element(field, 0), trees.tuple(std::array{value, field_type})});
}

Term analyze_record_attribute(ProcessContext &context, const Term &node) {
    const auto arguments = attribute_arguments_of(context, node, 2);
    const auto name = atom_name(context, arguments[0]);
    if (!type_is(context, arguments[1], "tuple")) {
        syntax_error(context);
    }
    Items fields;
    for (const auto &field : syntax::elements(get(context, arguments[1], "tuple_elements"))) {
        fields.push_back(analyze_record_field(context, field));
    }
    const Trees trees(context);
    return trees.tuple(std::array{name, trees.list(fields)});
}

// The {Name, Value} of each field of a record expression.
Term record_expr_values(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    Items fields;
    for (const auto &field : syntax::elements(record_expr_fields(context, node))) {
        const auto analysed = analyze_record_field(context, field);
        fields.push_back(trees.tuple(std::array{element(analysed, 0), element(element(analysed, 1), 0)}));
    }
    return trees.list(fields);
}

Term record_expr_info(ProcessContext &context, const Term &node) {
    const auto type = record_expr_type(context, node);
    const auto kind = type_of(context, type);
    if (kind != "atom" && kind != "list") {
        syntax_error(context);
    }
    const auto values = record_expr_values(context, node);
    const auto name = kind == "atom" ? get(context, type, "atom_value") : list_elements(context, type);
    const Trees trees(context);
    return trees.tuple(std::array{trees.atom("record_expr"_atom), trees.tuple(std::array{name, values})});
}

// {Tag, {Type, Field}} of a record access or index expression whose field and type are atoms.
template <FieldName FIELD>
Term record_field_info(ProcessContext &context, AtomName tag, const Term &node,
                       Term (*type_of_record)(ProcessContext &, const Term &)) {
    const auto field = get(context, node, FIELD);
    if (!type_is(context, field, "atom")) {
        syntax_error(context);
    }
    const auto record = atom_name(context, type_of_record(context, node));
    const Trees trees(context);
    return trees.tuple(std::array{trees.atom(tag), trees.tuple(std::array{record, get(context, field, "atom_value")})});
}

Term analyze_record_expr(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    if (type == "record_expr") {
        return record_expr_info(context, node);
    }
    if (type == "record_access") {
        return record_field_info<"record_access_field">(context, "record_access"_atom, node, record_access_type);
    }
    if (type == "record_index_expr") {
        return record_field_info<"record_index_expr_field">(context, "record_index_expr"_atom, node,
                                                            record_index_expr_type);
    }
    return Trees(context).atom_of(type);
}

Term analyze_function(ProcessContext &context, const Term &node) {
    if (!type_is(context, node, "function")) {
        syntax_error(context);
    }
    const auto name = atom_name(context, function_name(context, node));
    return Trees(context).tuple(std::array{name, function_arity(context, node)});
}

Term analyze_implicit_fun(ProcessContext &context, const Term &node) {
    if (!type_is(context, node, "implicit_fun")) {
        syntax_error(context);
    }
    return function_name_of(context, implicit_fun_name(context, node));
}

Term type_name_of(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    if (type == "atom") {
        return get(context, node, "atom_value");
    }
    if (type == "arity_qualifier") {
        const auto arity = get(context, node, "arity_qualifier_argument");
        const auto name = get(context, node, "arity_qualifier_body");
        if (!type_is(context, arity, "integer") || !type_is(context, name, "atom")) {
            syntax_error(context);
        }
        return with_arity(context, get(context, arity, "integer_value"), get(context, name, "atom_value"));
    }
    if (type != "module_qualifier") {
        syntax_error(context);
    }
    const auto module = atom_name(context, get(context, node, "module_qualifier_argument"));
    return Trees(context).tuple(std::array{module, type_name_of(context, get(context, node, "module_qualifier_body"))});
}

// What a name analysis gives, or the failure it raised (a term access failure is rethrown).
template <typename Analyse> std::optional<Term> attempt(Analyse analyse, std::optional<BuiltinFailure> &failure) {
    try {
        return analyse();
    } catch (const BuiltinFailure &raised) {
        if (raised.term) {
            throw;
        }
        failure = raised;
        return std::nullopt;
    }
}

Term analyze_application(ProcessContext &context, const Term &node) {
    if (!type_is(context, node, "application")) {
        syntax_error(context);
    }
    const auto arity = Trees(context).integer(std::int64_t(length(get(context, node, "application_arguments"))));
    const auto operator_node = get(context, node, "application_operator");
    std::optional<BuiltinFailure> failure;
    const auto name = attempt([&] { return function_name_of(context, operator_node); }, failure);
    if (name) {
        return with_arity(context, arity, *name);
    }
    if (!is_syntax_error(*failure, context)) {
        syntax_error(context);
    }
    return arity;
}

Term analyze_type_application(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    const auto user = type == "user_type_application";
    if (!user && type != "type_application") {
        syntax_error(context);
    }
    const auto arguments =
        user ? get(context, node, "user_type_application_arguments") : type_application_arguments(context, node);
    const auto arity = Trees(context).integer(std::int64_t(length(arguments)));
    const auto name = user ? user_type_application_name(context, node) : type_application_name(context, node);
    std::optional<BuiltinFailure> failure;
    const auto analysed = attempt([&] { return type_name_of(context, name); }, failure);
    if (!analysed) {
        syntax_error(context);
    }
    return with_arity(context, arity, *analysed);
}

// The preprocessor directives, which analyze_attribute/1 answers with `preprocessor`.
constexpr auto DIRECTIVES = std::to_array<std::string_view>(
    {"define", "undef", "include", "include_lib", "ifdef", "ifndef", "if", "elif", "else", "endif"});

// analyze_attribute/2: the analysis of a known attribute, the value of any other.
Term attribute_info(ProcessContext &context, std::string_view name, const Term &node) {
    if (name == "module") {
        return analyze_module_attribute(context, node);
    }
    if (name == "export") {
        return analyze_export_attribute(context, node);
    }
    if (name == "import") {
        return analyze_import_attribute(context, node);
    }
    if (name == "file") {
        return analyze_file_attribute(context, node);
    }
    if (name == "record") {
        return analyze_record_attribute(context, node);
    }
    return element(analyze_wild_attribute(context, node), 1);
}

Term analyze_attribute(ProcessContext &context, const Term &node) {
    const auto name = atom_name(context, attribute_name(context, node));
    const auto spelling = name.atom_spelling_unchecked();
    const Trees trees(context);
    if (std::ranges::find(DIRECTIVES, spelling) != DIRECTIVES.end()) {
        return trees.atom("preprocessor"_atom);
    }
    return trees.tuple(std::array{name, attribute_info(context, spelling, node)});
}

// The form types analyze_form/1 answers with the type itself.
constexpr auto OTHER_FORMS = std::to_array<std::string_view>({"comment", "eof_marker", "form_list", "text"});

Term analyze_form(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    const auto type = type_of(context, node);
    const auto tagged = [&](AtomName tag, const Term &info) { return trees.tuple(std::array{trees.atom(tag), info}); };
    if (type == "attribute") {
        return tagged("attribute"_atom, analyze_attribute(context, node));
    }
    if (type == "function") {
        return tagged("function"_atom, analyze_function(context, node));
    }
    if (type == "error_marker") {
        return tagged("error_marker"_atom, get(context, node, "error_marker_info"));
    }
    if (type == "warning_marker") {
        return tagged("warning_marker"_atom, get(context, node, "warning_marker_info"));
    }
    if (std::ranges::find(OTHER_FORMS, type) == OTHER_FORMS.end()) {
        syntax_error(context);
    }
    return trees.atom_of(type);
}

// The #forms{} record of analyze_forms/1: each field's items newest first, as OTP prepends them.
struct FormInfo {
    std::optional<Term> module;
    Items exports;
    Items imports;
    Items module_imports;
    Items attributes;
    Items records;
    Items errors;
    Items warnings;
    Items functions;
};

// Prepend `items` (in order) to `into`, as `L ++ Into` does.
void prepend_all(Items &into, const Items &items) { into.insert(into.begin(), items.begin(), items.end()); }

// finfo_add_imports/3: the functions join an earlier import of the same module, else a new entry comes first.
void add_imports(ProcessContext &context, FormInfo &info, const Term &module, const Term &functions) {
    const Trees trees(context);
    for (auto &entry : info.imports) {
        if (collections::compare(element(entry, 0), module) == 0) {
            entry = trees.tuple(std::array{module, trees.list(syntax::elements(functions), element(entry, 1))});
            return;
        }
    }
    info.imports.insert(info.imports.begin(), trees.tuple(std::array{module, functions}));
}

// collect_attribute/3 of the module, export and import attributes; false for any other.
bool collect_interface(ProcessContext &context, FormInfo &info, std::string_view name, const Term &data) {
    if (name == "module") {
        info.module = info.module ? info.module : data;
    } else if (name == "export") {
        prepend_all(info.exports, syntax::elements(data));
    } else if (name == "import" && data.is_tuple() && data.tuple_size_unchecked() == 2) {
        add_imports(context, info, element(data, 0), element(data, 1));
    } else if (name == "import") {
        info.module_imports.insert(info.module_imports.begin(), data);
    } else {
        return false;
    }
    return true;
}

// collect_attribute/3: records and other attributes after the interface; file attributes are left out.
void collect_attribute(ProcessContext &context, FormInfo &info, const Term &name, const Term &data) {
    const auto spelling = name.atom_spelling_unchecked();
    if (collect_interface(context, info, spelling, data) || spelling == "file") {
        return;
    }
    if (spelling == "record" && data.is_tuple() && data.tuple_size_unchecked() == 2) {
        info.records.insert(info.records.begin(), data);
        return;
    }
    info.attributes.insert(info.attributes.begin(), Trees(context).tuple(std::array{name, data}));
}

void collect_form(ProcessContext &context, FormInfo &info, const Term &form) {
    const auto analysed = analyze_form(context, form);
    if (!analysed.is_tuple()) {
        return;
    }
    const auto tag = element(analysed, 0).atom_spelling_unchecked();
    const auto data = element(analysed, 1);
    if (tag == "attribute" && data.is_tuple()) {
        collect_attribute(context, info, element(data, 0), element(data, 1));
    } else if (tag == "function") {
        info.functions.insert(info.functions.begin(), data);
    } else if (tag == "error_marker") {
        info.errors.insert(info.errors.begin(), data);
    } else if (tag == "warning_marker") {
        info.warnings.insert(info.warnings.begin(), data);
    }
}

// finfo_to_list/1: the keys that have a value, in OTP's order.
Term info_list(ProcessContext &context, const FormInfo &info) {
    const Trees trees(context);
    Items pairs;
    const auto add = [&](AtomName key, const Items &items) {
        if (!items.empty()) {
            pairs.push_back(trees.tuple(std::array{trees.atom(key), trees.list(items)}));
        }
    };
    if (info.module) {
        pairs.push_back(trees.tuple(std::array{trees.atom("module"_atom), *info.module}));
    }
    add("exports"_atom, info.exports);
    add("imports"_atom, info.imports);
    add("module_imports"_atom, info.module_imports);
    add("attributes"_atom, info.attributes);
    add("records"_atom, info.records);
    add("errors"_atom, info.errors);
    add("warnings"_atom, info.warnings);
    add("functions"_atom, info.functions);
    return trees.list(pairs);
}

Term analyze_forms(ProcessContext &context, const Term &forms) {
    const auto list = forms.is_list() ? forms : get(context, flatten_form_list(context, forms), "form_list_elements");
    FormInfo info;
    for (const auto &form : syntax::elements(list)) {
        collect_form(context, info, form);
    }
    return info_list(context, info);
}

// erlang:exit/1, throw/1, error/1,2 and fault/1,2.
bool is_erlang_fail(const Term &function, std::size_t arity) {
    if (arity == 1 && (is_atom(function, "exit") || is_atom(function, "throw"))) {
        return true;
    }
    return (arity == 1 || arity == 2) && (is_atom(function, "error") || is_atom(function, "fault"));
}

// The names exit/1, throw/1 and the erlang ones that is_fail_expr/1 knows.
bool is_fail_name(const Term &name, std::size_t arity) {
    if (name.is_atom()) {
        return arity == 1 && (is_atom(name, "exit") || is_atom(name, "throw"));
    }
    const auto qualified = name.is_tuple() && name.tuple_size_unchecked() == 2 && is_atom(element(name, 0), "erlang");
    return qualified && is_erlang_fail(element(name, 1), arity);
}

Term is_fail_expr(ProcessContext &context, const Term &node) {
    const Trees trees(context);
    if (!type_is(context, node, "application")) {
        return trees.boolean(false);
    }
    const auto arity = length(get(context, node, "application_arguments"));
    const auto operator_node = get(context, node, "application_operator");
    std::optional<BuiltinFailure> failure;
    const auto name = attempt([&] { return function_name_of(context, operator_node); }, failure);
    return trees.boolean(name && is_fail_name(*name, arity));
}

// function_name_expansions/3: the short names of one name, each paired with the name, newest first.
void expansions(ProcessContext &context, const Term &name, const Term &full, Items &into) {
    const Trees trees(context);
    if (name.is_tuple() && name.tuple_size_unchecked() == 2) {
        if (element(name, 1).is_integer()) {
            into.push_back(trees.tuple(std::array{name, full}));
            return;
        }
        expansions(context, element(name, 1), full, into);
        return;
    }
    into.push_back(trees.tuple(std::array{name, full}));
}

Term function_name_expansions(ProcessContext &context, const Term &names) {
    Items expanded;
    for (const auto &name : syntax::elements(names)) {
        expansions(context, name, name, expanded);
    }
    std::ranges::reverse(expanded);
    return Trees(context).list(expanded);
}

Term analyze_function_name(ProcessContext &context, const Term &node) { return function_name_of(context, node); }

Term analyze_type_name(ProcessContext &context, const Term &node) { return type_name_of(context, node); }

constexpr std::array ANALYSES{
    typed_entry<analyze_application>("erl_syntax_lib", "analyze_application"),
    typed_entry<analyze_attribute>("erl_syntax_lib", "analyze_attribute"),
    typed_entry<analyze_export_attribute>("erl_syntax_lib", "analyze_export_attribute"),
    typed_entry<analyze_file_attribute>("erl_syntax_lib", "analyze_file_attribute"),
    typed_entry<analyze_form>("erl_syntax_lib", "analyze_form"),
    typed_entry<analyze_forms>("erl_syntax_lib", "analyze_forms"),
    typed_entry<analyze_function>("erl_syntax_lib", "analyze_function"),
    typed_entry<analyze_function_name>("erl_syntax_lib", "analyze_function_name"),
    typed_entry<analyze_implicit_fun>("erl_syntax_lib", "analyze_implicit_fun"),
    typed_entry<analyze_import_attribute>("erl_syntax_lib", "analyze_import_attribute"),
    typed_entry<analyze_module_attribute>("erl_syntax_lib", "analyze_module_attribute"),
    typed_entry<analyze_record_attribute>("erl_syntax_lib", "analyze_record_attribute"),
    typed_entry<analyze_record_expr>("erl_syntax_lib", "analyze_record_expr"),
    typed_entry<analyze_record_field>("erl_syntax_lib", "analyze_record_field"),
    typed_entry<analyze_type_application>("erl_syntax_lib", "analyze_type_application"),
    typed_entry<analyze_type_name>("erl_syntax_lib", "analyze_type_name"),
    typed_entry<analyze_wild_attribute>("erl_syntax_lib", "analyze_wild_attribute"),
    typed_entry<is_fail_expr>("erl_syntax_lib", "is_fail_expr"),
    typed_entry<function_name_expansions>("erl_syntax_lib", "function_name_expansions"),
};
} // namespace

Term function_name_of(ProcessContext &context, const Term &node) {
    const auto type = type_of(context, node);
    if (type == "atom") {
        return get(context, node, "atom_value");
    }
    if (type == "arity_qualifier") {
        const auto arity = get(context, node, "arity_qualifier_argument");
        if (!type_is(context, arity, "integer")) {
            syntax_error(context);
        }
        const auto name = function_name_of(context, get(context, node, "arity_qualifier_body"));
        return with_arity(context, get(context, arity, "integer_value"), name);
    }
    if (type != "module_qualifier") {
        syntax_error(context);
    }
    const auto module = atom_name(context, get(context, node, "module_qualifier_argument"));
    return Trees(context).tuple(
        std::array{module, function_name_of(context, get(context, node, "module_qualifier_body"))});
}
} // namespace clause::runtime::builtins::syntax_lib

namespace clause::runtime {
// Publish erl_syntax_lib's analyses.
std::span<const BuiltinEntry> erl_syntax_lib_analysis_builtins() noexcept { return builtins::syntax_lib::ANALYSES; }
} // namespace clause::runtime
