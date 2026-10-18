#include "native_records.hpp"
#include "record_definitions.hpp"
#include "records.hpp"
#include <algorithm>

namespace clause::semantic {
void native_defaults(const Module &module, const RecordLayout &layout, const Reporter &out) {
    for (const auto &field : layout.fields) {
        if (field.default_value && !default_folds(*module.syntax, *field.default_value)) {
            report(module, &module.syntax->expression(*field.default_value).source,
                   "illegal default value for field " + utf8(field.name.name) + " in native record " +
                       utf8(layout.name.name),
                   out);
        }
    }
}

void native_initialized(const Module &module, const ast::RecordExpression &record, const RecordLayout &layout,
                        const std::set<std::u32string> &named, const Reporter &out) {
    for (const auto &field : layout.fields) {
        if (!field.default_value && !named.contains(field.name.name)) {
            report(module, &record.identity.source,
                   "field " + utf8(field.name.name) + " is not initialized in native record " + utf8(layout.name.name),
                   out);
        }
    }
}
} // namespace clause::semantic

namespace clause::semantic {
namespace {
// The names of an -export_record list; nullopt unless it is a proper list of atoms.
std::optional<std::vector<std::u32string>> export_names(const ast::Module &syntax, const ast::TermId &id) {
    const auto *list = std::get_if<ast::TermList>(&syntax.term(id).value);
    if (!list || list->tail) {
        return std::nullopt;
    }
    std::vector<std::u32string> names;
    names.reserve(list->elements.size());
    for (const auto &element : list->elements) {
        const auto *atom = std::get_if<ast::Atom>(&syntax.term(element).value);
        if (!atom) {
            return std::nullopt;
        }
        names.push_back(atom->name);
    }
    return names;
}

// One exported name and the attribute that exported it, checked once every declaration is known.
struct Export {
    std::u32string name;
    const ast::NodeSource *source;
};

// -export_record precedes the first function and lists record names.
void export_attribute(const Module &module, const ast::GenericAttribute &attribute, const ast::NodeSource &source,
                      const bool functions, std::vector<Export> &exports, const Reporter &out) {
    if (functions) {
        report(module, &source, "attribute export_record after function definitions", out);
    }
    const auto names = export_names(*module.syntax, attribute.value);
    if (!names) {
        report(module, &source, "badly formed -export_record(); expected a list of record names", out);
        return;
    }
    for (const auto &name : *names) {
        exports.push_back({name, &source});
    }
}

// A name is imported from one module and never also declared in this one.
void import_attribute(Module &module, const ast::ImportRecordAttribute &attribute, const ast::NodeSource &source,
                      const std::size_t order, const Reporter &out) {
    for (const auto &name : attribute.names) {
        const auto imported = module.imported_records.find(name.name);
        const auto declared = module.record_order.find(name.name);
        if (imported != module.imported_records.end()) {
            report(module, &source, "record " + utf8(name.name) + " already imported from " + utf8(imported->second),
                   out);
        } else if (declared != module.record_order.end()) {
            report(module, &source,
                   declared->second < order
                       ? "record " + utf8(name.name) + " is already defined locally"
                       : "record " + utf8(name.name) + " already imported from " + utf8(attribute.module.name),
                   out);
        } else {
            module.imported_records.emplace(name.name, attribute.module.name);
        }
    }
}

// Only native records of this module can be exported.
void exported(Module &module, const Export &item, const Reporter &out) {
    const auto found = module.records.find(item.name);
    if (found == module.records.end()) {
        report(module, item.source, "native record " + utf8(item.name) + " undefined", out);
    } else if (!found->second.native) {
        report(module, item.source, "tuple records cannot be exported; only native records can", out);
    } else {
        module.exported_records.insert(item.name);
    }
}
} // namespace

void index_record_attributes(Module &module, const Reporter &out) {
    std::vector<Export> exports;
    bool functions = false;
    std::size_t order = 0;
    for (const auto &id : module.syntax->forms()) {
        const auto &form = module.syntax->form(id);
        if (const auto *generic = std::get_if<ast::GenericAttribute>(&form.value);
            generic && generic->name.name == U"export_record") {
            export_attribute(module, *generic, form.source, functions, exports, out);
        } else if (const auto *imported = std::get_if<ast::ImportRecordAttribute>(&form.value)) {
            import_attribute(module, *imported, form.source, order, out);
        }
        functions = functions || std::holds_alternative<ast::Function>(form.value);
        ++order;
    }
    for (const auto &item : exports) {
        exported(module, item, out);
    }
}

void external_fields(const Module &module, const ast::RecordExpression &record, const Reporter &out) {
    std::set<std::u32string> names;
    for (const auto &field : record.fields) {
        const auto *name = std::get_if<ast::Atom>(&field.name);
        if (!name) {
            report(module, &field.source,
                   "multi-field initialization (assigning to _) is only supported for tuple records", out);
        } else if (!names.insert(name->name).second) {
            report(module, &field.source, "duplicate record initializer", out);
        }
    }
}

std::optional<RecordName> external_record(const Module &module, const ast::RecordIdentity &identity) {
    if (const auto *qualified = std::get_if<ast::QualifiedRecordName>(&identity.value)) {
        return RecordName{qualified->module.name, qualified->name.name};
    }
    const auto *local = std::get_if<ast::UnresolvedRecordName>(&identity.value);
    if (!local || record_layout(module, identity)) {
        return std::nullopt;
    }
    const auto *from = imported_module(module, local->name.name);
    return from ? std::optional{RecordName{*from, local->name.name}} : std::nullopt;
}

bool anonymous_record(const ast::RecordIdentity &identity) {
    return std::holds_alternative<ast::InferredRecordName>(identity.value);
}

const std::u32string *imported_module(const Module &module, const std::u32string &name) {
    const auto found = module.imported_records.find(name);
    return found == module.imported_records.end() ? nullptr : &found->second;
}

const RecordLayout *external_layout(const Module &module, const RecordName &record) {
    const auto peer = module.peers.find(record.module);
    if (peer == module.peers.end()) {
        return nullptr;
    }
    const auto found = peer->second->records.find(record.name);
    if (found == peer->second->records.end() || !found->second.native ||
        !peer->second->exported_records.contains(record.name)) {
        return nullptr;
    }
    return &found->second;
}
} // namespace clause::semantic
