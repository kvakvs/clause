#include "records.hpp"
#include "binding_state.hpp"
#include "capabilities.hpp"
#include "native_records.hpp"
#include <algorithm>
#include <set>

namespace clause::semantic {
RecordLayout::RecordLayout(const ast::RecordDeclaration &declaration)
    : name(declaration.name), native(declaration.native), fields(declaration.fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        positions.emplace(fields[i].name.name, i);
    }
}

namespace {
// The first origin belongs to the immutable per-form table even for nested source extents.
const ast::TokenOrigin *origin(const Module &module, ast::NodeSource source) {
    source.begin = 0;
    return module.syntax->extent(source).data();
}

// Preserve source order so a later declaration cannot authorize an earlier function or default.
void declaration(Module &module, const ast::Form &form, std::size_t order, const Reporter &out) {
    module.source_order.emplace(origin(module, form.source), order);
    const auto *record = std::get_if<ast::RecordDeclaration>(&form.value);
    if (!record) {
        return;
    }
    if (!module.records.emplace(record->name.name, RecordLayout{*record}).second) {
        report(module, &form.source, "duplicate record declaration", out);
    }
    module.record_order.emplace(record->name.name, order);
    std::set<std::u32string> names;
    for (const auto &field : record->fields) {
        if (!names.insert(field.name.name).second) {
            report(module, &field.source, "duplicate record field " + utf8(field.name.name), out);
        }
    }
}

// Unknown ordinary names are errors; native and imported identities are resolved by their deferred owners.
const RecordLayout *required(const Module &module, const ast::RecordIdentity &identity, const Reporter &out) {
    const auto *layout = record_layout(module, identity);
    const auto *name = std::get_if<ast::UnresolvedRecordName>(&identity.value);
    if (!layout && name && !imported_module(module, name->name.name)) {
        report(module, &identity.source, "undefined record", out);
    }
    return layout;
}

// Explicit names are unique; a checked layout must declare each of them.
void explicit_field(const Module &module, const ast::RecordField &field, const ast::Atom &name,
                    const RecordLayout *layout, std::set<std::u32string> &names, const Reporter &out) {
    if (layout && !record_field(*layout, name)) {
        report(module, &field.source, "undefined record field " + utf8(name.name), out);
    }
    if (!names.insert(name.name).second) {
        report(module, &field.source, "duplicate record initializer", out);
    }
}

// Wildcards initialize omitted fields only and cannot repeat.
void wildcard_field(const Module &module, const ast::RecordField &field, const ast::NodeSource *&wildcard,
                    const Reporter &out) {
    if (std::get<ast::Variable>(field.name).name != U"_") {
        report(module, &field.source, "record wildcard field must be '_'", out);
    }
    if (wildcard) {
        report(module, &field.source, "duplicate record wildcard initializer", out);
    }
    wildcard = &field.source;
}

// Native records take no wildcard; an update wildcard is meaningless; a pattern's must cover an omitted field
// (a construction's then initializes nothing and is not evaluated, as in OTP).
void wildcard_rules(const Module &module, const ast::RecordExpression &record, const RecordLayout &layout,
                    const bool pattern, const std::size_t named, const ast::NodeSource *wildcard, const Reporter &out) {
    if (!wildcard) {
        return;
    }
    if (layout.native) {
        report(module, wildcard, "multi-field initialization (assigning to _) is only supported for tuple records",
               out);
    } else if (record.base) {
        report(module, wildcard, "meaningless use of _ in update of record " + utf8(layout.name.name), out);
    } else if (pattern && named >= layout.fields.size()) {
        report(module, wildcard, "record wildcard requires an omitted field", out);
    }
}

// Native records carry their own field list, so only their construction must name declared fields (OTP warns
// for updates and patterns) and give every field a value.
void fields(const Module &module, const ast::RecordExpression &record, const RecordLayout &layout, const bool pattern,
            const Reporter &out) {
    const bool construction = !pattern && !record.base;
    const auto *checked = !layout.native || construction ? &layout : nullptr;
    std::set<std::u32string> names;
    const ast::NodeSource *wildcard = nullptr;
    for (const auto &field : record.fields) {
        if (const auto *name = std::get_if<ast::Atom>(&field.name)) {
            explicit_field(module, field, *name, checked, names, out);
        } else {
            wildcard_field(module, field, wildcard, out);
        }
    }
    wildcard_rules(module, record, layout, pattern, names.size(), wildcard, out);
    if (layout.native && construction) {
        native_initialized(module, record, layout, names, out);
    }
}

// Values of the atom-named fields in source order; a native record pattern lists only those.
std::vector<ast::ExprId> named_values(const ast::RecordExpression &record) {
    std::vector<ast::ExprId> result;
    result.reserve(record.fields.size());
    for (const auto &field : record.fields) {
        if (std::holds_alternative<ast::Atom>(field.name)) {
            result.push_back(field.value);
        }
    }
    return result;
}

// Append the declared positions of a record's explicit fields in source order, each once.
void explicit_positions(const ast::RecordExpression &record, const RecordLayout &layout, std::vector<bool> &placed,
                        std::vector<std::size_t> &result) {
    for (const auto &field : record.fields) {
        const auto *name = std::get_if<ast::Atom>(&field.name);
        const auto position = name ? record_field(layout, *name) : std::nullopt;
        if (position && !placed[*position]) {
            placed[*position] = true;
            result.push_back(*position);
        }
    }
}

// Defaults cannot capture or create ordinary named variables; closure scopes retain their own legality rules.
bool arity_literal(const Module &module, const ast::ExprId &id) {
    const auto &value = module.syntax->expression(ungroup(*module.syntax, id)).value;
    return std::holds_alternative<ast::IntegerLiteral>(value) || std::holds_alternative<ast::Atom>(value);
}

// Defaults cannot capture or create ordinary named variables; closure scopes retain their own legality rules.
void defaults(const Module &module, std::vector<ast::ExprId> pending, const Reporter &out) {
    std::size_t work = 0;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto &expression = module.syntax->expression(id);
        if (++work > 1'000'000) {
            report(module, &expression.source, "record default work limit exceeded", out);
            return;
        }
        validate_record(module, expression, out);
        const auto *variable = std::get_if<ast::Variable>(&expression.value);
        if (variable && variable->name != U"_") {
            report(module, &expression.source, "named variable is illegal in a record default", out);
        }
        const auto children = binding_children(expression.value);
        pending.insert(pending.end(), children.begin(), children.end());
    }
}

struct Initializers {
    // Explicit names override the wildcard; defaults remain in the borrowed declaration.
    std::map<std::u32string, ast::ExprId> values;
    std::optional<ast::ExprId> wildcard;
};

// Index only explicit syntax once, keeping selected-field evaluation in declaration order.
void initializers(Initializers &result, const ast::RecordExpression &record) {
    for (const auto &field : record.fields) {
        if (const auto *name = std::get_if<ast::Atom>(&field.name)) {
            result.values.emplace(name->name, field.value);
        } else {
            result.wildcard = field.value;
        }
    }
}

// Pattern omissions constrain nothing, whereas construction omissions evaluate their defaults.
std::optional<ast::ExprId> selected(const Initializers &initializers, const ast::RecordDeclarationField &field,
                                    const bool pattern) {
    const auto found = initializers.values.find(field.name.name);
    if (found != initializers.values.end()) {
        return found->second;
    }
    if (initializers.wildcard) {
        return initializers.wildcard;
    }
    return pattern ? std::nullopt : field.default_value;
}

// A record_info/2 argument must be a literal atom; parentheses vanish as in OTP's abstract format.
const ast::Atom *info_argument(const ast::Module &syntax, const ast::ExprId &id) {
    return std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, id)).value);
}

// OTP rejects non-literal arguments and selectors other than fields/size, then any name that is not a visible
// tuple record (an undefined name included).
void validate_record_info(const Module &module, const ast::Expression &expression, const Reporter &out) {
    const auto &call = std::get<ast::CallExpression>(expression.value);
    const auto &selector = module.syntax->expression(call.arguments[0]);
    const auto *info = info_argument(*module.syntax, call.arguments[0]);
    if (!info || !info_argument(*module.syntax, call.arguments[1])) {
        report(module, &expression.source, "illegal record info", out);
    } else if (info->name != U"fields" && info->name != U"size") {
        report(module, &selector.source, "illegal record info", out);
    } else if (!record_info(module, expression)) {
        report(module, &selector.source, "record_info/2 is only supported for tuple records", out);
    }
}

// Tuple indices are constants only for ordinary records with a visible declared field.
void validate_index(const Module &module, const ast::RecordIndex &index, const ast::NodeSource &source,
                    const Reporter &out) {
    const auto *layout = record_layout(module, index.record, source);
    if (!layout) {
        report(module, &index.name_source, "undefined record", out);
    } else if (layout->native) {
        report(module, &index.name_source,
               "syntax #" + utf8(index.record.name) + "." + utf8(index.field.name) +
                   " is only supported for tuple records",
               out);
    } else if (!record_field(*layout, index.field)) {
        report(module, &index.field_source, "undefined record field", out);
    }
}
} // namespace

void index_records(Module &module, const Reporter &out) {
    std::size_t order = 0;
    std::vector<ast::ExprId> pending;
    for (const auto &id : module.syntax->forms()) {
        declaration(module, module.syntax->form(id), order++, out);
    }
    index_record_attributes(module, out);
    for (const auto &[name, layout] : module.records) {
        (void)name;
        if (layout.native) {
            native_defaults(module, layout, out);
        }
        for (const auto &field : layout.fields) {
            if (field.default_value) {
                pending.push_back(*field.default_value);
            }
        }
    }
    defaults(module, std::move(pending), out);
}

bool record_budget(BindingAnalysis &state, const ast::ExprId &id) {
    const auto *record = std::get_if<ast::RecordExpression>(&state.module.syntax->expression(id).value);
    const auto *layout = record ? record_layout(state.module, record->identity) : nullptr;
    return !layout || state.spend(id, layout->fields.size() + record->fields.size() + 1);
}

const RecordLayout *record_layout(const Module &module, const ast::Atom &name, const ast::NodeSource &source) {
    const auto found = module.records.find(name.name);
    if (found == module.records.end() ||
        module.record_order.at(name.name) >= module.source_order.at(origin(module, source))) {
        return nullptr;
    }
    return &found->second;
}

const RecordLayout *record_layout(const Module &module, const ast::RecordIdentity &identity) {
    const auto *name = std::get_if<ast::UnresolvedRecordName>(&identity.value);
    return name ? record_layout(module, name->name, identity.source) : nullptr;
}

std::optional<std::size_t> record_field(const RecordLayout &layout, const ast::Atom &name) {
    const auto found = layout.positions.find(name.name);
    return found == layout.positions.end() ? std::nullopt : std::optional{found->second};
}

std::vector<std::optional<ast::ExprId>> record_values(const Module &module, const ast::RecordExpression &record,
                                                      const bool pattern) {
    const auto *layout = record_layout(module, record.identity);
    if (!layout) {
        return {};
    }
    Initializers values;
    initializers(values, record);
    std::vector<std::optional<ast::ExprId>> result;
    result.reserve(layout->fields.size());
    for (const auto &field : layout->fields) {
        result.push_back(selected(values, field, pattern));
    }
    return result;
}

std::optional<ast::ExprId> unused_wildcard(const Module &module, const ast::RecordExpression &record) {
    const auto *layout = record.base ? nullptr : record_layout(module, record.identity);
    if (!layout || layout->native) {
        return std::nullopt;
    }
    Initializers values;
    initializers(values, record);
    const auto omitted = [&](const ast::RecordDeclarationField &field) {
        return !values.values.contains(field.name.name);
    };
    return std::ranges::any_of(layout->fields, omitted) ? std::nullopt : values.wildcard;
}

namespace {
// Anonymous records cannot be built; anonymous and external forms check only their field list.
void validate_expression(const Module &module, const ast::RecordExpression &record, const bool pattern,
                         const Reporter &out) {
    const bool anonymous = anonymous_record(record.identity);
    if (anonymous && !pattern && !record.base) {
        report(module, &record.identity.source, "native record '_' undefined", out);
    } else if (anonymous || external_record(module, record.identity)) {
        external_fields(module, record, out);
    } else if (const auto *layout = required(module, record.identity, out)) {
        fields(module, record, *layout, pattern, out);
    }
}

// Tuple-record access needs a declared field; native records carry their own.
void validate_access(const Module &module, const ast::RecordAccess &access, const Reporter &out) {
    const auto *layout = required(module, access.identity, out);
    if (layout && !layout->native && !record_field(*layout, access.field)) {
        report(module, &access.field_source, "undefined record field", out);
    }
}
} // namespace

void validate_record(const Module &module, const ast::Expression &expression, const Reporter &out, const bool pattern) {
    if (const auto *record = std::get_if<ast::RecordExpression>(&expression.value)) {
        validate_expression(module, *record, pattern, out);
    } else if (const auto *access = std::get_if<ast::RecordAccess>(&expression.value)) {
        validate_access(module, *access, out);
    } else if (const auto *index = std::get_if<ast::RecordIndex>(&expression.value)) {
        validate_index(module, *index, expression.source, out);
    } else if (record_info_call(*module.syntax, expression.value)) {
        validate_record_info(module, expression, out);
    }
}

std::vector<std::size_t> record_order(const Module &module, const ast::RecordExpression &record) {
    const auto *layout = record_layout(module, record.identity);
    std::vector<std::size_t> result;
    if (!layout) {
        return result;
    }
    result.reserve(layout->fields.size());
    std::vector<bool> placed(layout->fields.size());
    if (layout->native) {
        explicit_positions(record, *layout, placed, result);
    }
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (!placed[i]) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<const RecordLayout *> native_layouts(const Module &module) {
    std::vector<const RecordLayout *> result;
    for (const auto &[name, layout] : module.records) {
        (void)name;
        if (layout.native) {
            result.push_back(&layout);
        }
    }
    return result;
}

std::vector<ast::ExprId> pattern_fields(const Module &module, const ast::RecordExpression &record) {
    const auto *layout = record_layout(module, record.identity);
    if (!layout || layout->native) {
        return named_values(record);
    }
    std::vector<ast::ExprId> result;
    for (const auto &field : record_values(module, record, true)) {
        if (field) {
            result.push_back(*field);
        }
    }
    return result;
}

bool record_info_call(const ast::Module &syntax, const ast::ExprValue &value) {
    const auto *call = std::get_if<ast::CallExpression>(&value);
    const auto *name = call ? std::get_if<ast::Atom>(&syntax.expression(ungroup(syntax, call->target)).value) : nullptr;
    return name && name->name == U"record_info" && call->arguments.size() == 2;
}

std::optional<RecordInfo> record_info(const Module &module, const ast::Expression &expression) {
    if (!record_info_call(*module.syntax, expression.value)) {
        return {};
    }
    const auto &call = std::get<ast::CallExpression>(expression.value);
    const auto *info = info_argument(*module.syntax, call.arguments[0]);
    const auto *name = info_argument(*module.syntax, call.arguments[1]);
    if (!info || !name || (info->name != U"fields" && info->name != U"size")) {
        return {};
    }
    const auto *layout = record_layout(module, *name, expression.source);
    if (!layout || layout->native) {
        return {};
    }
    return RecordInfo{*layout, info->name == U"fields"};
}

void validate_record_test(const Module &module, const ast::Expression &expression, const ast::CallExpression &call,
                          const bool guard, const Reporter &out) {
    const auto &tag = module.syntax->expression(ungroup(*module.syntax, call.arguments.at(1)));
    const auto *name = std::get_if<ast::Atom>(&tag.value);
    if (guard && !name) {
        report(module, &tag.source, "record guard tag must be a literal atom", out);
    }
    if (call.arguments.size() == 2 && name && !record_layout(module, *name, expression.source) &&
        !imported_module(module, name->name)) {
        report(module, &tag.source, "undefined record", out);
    }
    if (guard && call.arguments.size() == 3) {
        const auto &size = module.syntax->expression(ungroup(*module.syntax, call.arguments[2]));
        if (!arity_literal(module, call.arguments[2])) {
            report(module, &size.source, "record guard arity must be a literal integer or atom", out);
        }
    }
}
} // namespace clause::semantic
