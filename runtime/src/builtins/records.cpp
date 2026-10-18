#include "../terms/records.hpp"
#include "../memory/heap_object.hpp"
#include "erlang/term_decoding.hpp"
#include "string/string.hpp"
#include "typed.hpp"
#include <algorithm>
#include <array>
#include <clause/runtime/code_server.hpp>

// The records module (plan 21 step LIB8B, docs/native-records.md#reflection): native record reflection over the
// definition each record cell captures. create/4 interns run-time definitions in the code server, get_definition/2
// decodes the field list the compiler stored in the record's descriptor.
namespace clause::runtime::builtins {
namespace {
using string::Terms;

// The roles of the records functions' arguments (typed.hpp's Named).
struct KeyRole;
struct RecordRole;
struct ModuleRole;
struct NameRole;
struct FieldsRole;
struct OptionsRole;
using KeyArg = Named<KeyRole>;
using RecordArg = Named<RecordRole>;
using ModuleArg = Named<ModuleRole>;
using NameArg = Named<NameRole>;
// create/4's [{Field, Value}] and update/4's #{Field => Value}.
using FieldsArg = Named<FieldsRole>;
using OptionsArg = Named<OptionsRole>;

Term of(Word word) { return detail::TermAccess::make(word); }

[[noreturn]] void raise(abi::v1::ErrorReason reason, const Term &payload) {
    throw BuiltinFailure{.reason = reason, .payload = payload.word()};
}

// The record's view, or badarg (get/2, get_module/1, get_name/1).
detail::RecordView view_or_badarg(const RecordArg &record) {
    const auto view = detail::record_view(record.term);
    if (!view) {
        bad_argument();
    }
    return *view;
}

// The record's view, or {badrecord, Record} (get_field_names/1, is_exported/1, update/4).
detail::RecordView view_or_badrecord(const RecordArg &record) {
    const auto view = detail::record_view(record.term);
    if (!view) {
        raise(abi::v1::ErrorReason::badrecord, record.term);
    }
    return *view;
}

// {{Module, Name}, Field}: the payload of badfield.
Term field_payload(const Terms &terms, const RecordDefinition &definition, const Term &field) {
    const std::array owner{of(definition.module), of(definition.name)};
    const std::array payload{terms.tuple(owner), field};
    return terms.tuple(payload);
}

// get(Key, Record): the value of field Key; badarg for a missing field or a non-record.
Term get(ProcessContext & /*context*/, const KeyArg &key, const RecordArg &record) {
    const auto view = view_or_badarg(record);
    const auto position =
        key.term.is_atom() ? detail::record_position(*view.definition, key.term.word()) : std::nullopt;
    if (!position) {
        bad_argument();
    }
    return of(view.values[*position]);
}

Term get_module(ProcessContext & /*context*/, const RecordArg &record) {
    return of(view_or_badarg(record).definition->module);
}

Term get_name(ProcessContext & /*context*/, const RecordArg &record) {
    return of(view_or_badarg(record).definition->name);
}

// get_field_names(Record): the field names in definition order.
Term get_field_names(ProcessContext &context, const RecordArg &record) {
    const auto &fields = view_or_badrecord(record).definition->fields;
    std::vector<Term> names;
    names.reserve(fields.size());
    std::ranges::transform(fields, std::back_inserter(names), of);
    return Terms(context).list(names);
}

Term is_exported(ProcessContext &context, const RecordArg &record) {
    return Terms(context).boolean(view_or_badrecord(record).definition->exported);
}

// create/4's Options: exactly #{is_exported => Boolean}; {badmap, Options} for a non-map, badarg otherwise.
bool exported_option(const OptionsArg &options) {
    if (!options.term.is_map()) {
        raise(abi::v1::ErrorReason::badmap, options.term);
    }
    const auto entries = need(options.term.map_entries());
    if (entries.size() != 1 || !entries[0].first.is_atom() ||
        entries[0].first.atom_spelling_unchecked() != "is_exported") {
        bad_argument();
    }
    const auto &value = entries[0].second;
    if (!value.is_atom() || (value.atom_spelling_unchecked() != "true" && value.atom_spelling_unchecked() != "false")) {
        bad_argument();
    }
    return value.atom_spelling_unchecked() == "true";
}

// create/4's Fields in one pass, as OTP checks them: badarg for an element that is not a pair or an improper list,
// {badfield, {{Module, Name}, Field}} for a field name that is not an atom.
std::vector<std::pair<Term, Term>> field_pairs(ProcessContext &context, const NameArg &name, const ModuleArg &module,
                                               const FieldsArg &fields) {
    std::vector<std::pair<Term, Term>> pairs;
    auto rest = fields.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto pair = rest.head_unchecked();
        if (!pair.is_tuple() || pair.tuple_size_unchecked() != 2) {
            bad_argument();
        }
        const auto field = pair.tuple_element_unchecked(0);
        if (!field.is_atom()) {
            const Terms terms(context);
            const std::array owner{module.term, name.term};
            const std::array payload{terms.tuple(owner), field};
            raise(abi::v1::ErrorReason::badfield, terms.tuple(payload));
        }
        pairs.emplace_back(field, pair.tuple_element_unchecked(1));
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return pairs;
}

// create(Module, Name, Fields, Options): a record of a definition built from the arguments, in the given field order.
Term create(ProcessContext &context, const ModuleArg &module, const NameArg &name, const FieldsArg &fields,
            const OptionsArg &options) {
    if (!module.term.is_atom() || !name.term.is_atom()) {
        raise(abi::v1::ErrorReason::badrecord, name.term);
    }
    const bool exported = exported_option(options);
    const auto pairs = field_pairs(context, name, module, fields);
    CreatedRecordKey key{.module = module.term.word(), .name = name.term.word(), .exported = exported, .fields = {}};
    std::vector<Term> values;
    for (const auto &[field, value] : pairs) {
        key.fields.push_back(field.word());
        values.push_back(value);
    }
    auto sorted = key.fields;
    std::ranges::sort(sorted);
    if (std::ranges::adjacent_find(sorted) != sorted.end()) {
        bad_argument();
    }
    const auto &definition = context.code_server().created_record(key);
    return need(TermFactory(context).native_record(definition, values));
}

// update(Src, Module, Name, FieldsMap): Src with the map's fields replaced; {badrecord, Name} unless Src is a record
// of Module and Name.
Term update(ProcessContext &context, const RecordArg &source, const ModuleArg &module, const NameArg &name,
            const FieldsArg &fields) {
    const auto view = view_or_badrecord(source);
    const auto &definition = *view.definition;
    if (definition.module != module.term.word() || definition.name != name.term.word()) {
        raise(abi::v1::ErrorReason::badrecord, name.term);
    }
    if (!fields.term.is_map()) {
        raise(abi::v1::ErrorReason::badmap, fields.term);
    }
    std::vector<Word> values(view.values.begin(), view.values.end());
    for (const auto &[field, value] : need(fields.term.map_entries())) {
        const auto position = field.is_atom() ? detail::record_position(definition, field.word()) : std::nullopt;
        if (!position) {
            raise(abi::v1::ErrorReason::badfield, field_payload(Terms(context), definition, field));
        }
        values[*position] = value.word();
    }
    return need(TermFactory(context).native_record_words(definition, values));
}

// get_definition(Module, Name): {#{is_exported => Boolean}, Fields} of a compiled record; badarg when none.
Term get_definition(ProcessContext &context, const ModuleArg &module, const NameArg &name) {
    const auto *definition =
        module.term.is_atom() && name.term.is_atom()
            ? context.code_server().module_record({.module = module.term.word(), .name = name.term.word()})
            : nullptr;
    if (definition == nullptr || definition->definition.empty()) {
        bad_argument();
    }
    const Terms terms(context);
    const auto fields = decode_external(context, definition->definition);
    const std::array option{std::pair{terms.atom("is_exported"), terms.boolean(definition->exported)}};
    const std::array result{need(TermFactory(context).map(option)), fields};
    return terms.tuple(result);
}

constexpr std::array RECORDS_BUILTINS{
    typed_entry<create>("records", "create"),
    typed_entry<get>("records", "get"),
    typed_entry<get_definition>("records", "get_definition"),
    typed_entry<get_field_names>("records", "get_field_names"),
    typed_entry<get_module>("records", "get_module"),
    typed_entry<get_name>("records", "get_name"),
    typed_entry<is_exported>("records", "is_exported"),
    typed_entry<update>("records", "update"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> records_builtins() noexcept { return builtins::RECORDS_BUILTINS; }
} // namespace clause::runtime
