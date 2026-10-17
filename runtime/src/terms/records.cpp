#include "records.hpp"
#include "../memory/heap_object.hpp"
#include "../memory/heap_storage.hpp"
#include "term_layout.hpp"
#include "terms.hpp"
#include <algorithm>
#include <clause/runtime/process_context.hpp>
#include <new>

namespace clause::runtime {
namespace {
// Keep allocation failures distinct from the configured backing ceiling.
TermError heap_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// The ABI word of a field given as a Term or as a word.
Word word_of(const Term &value) { return value.word(); }

Word word_of(Word value) { return value; }

// Publish the header, the definition word and every field value as one immutable object.
template <typename Field>
TermResult<Term> record(ProcessHeap &heap, const std::shared_ptr<detail::HeapStorage> &storage,
                        const RecordDefinition &definition, std::span<const Field> fields) {
    auto reserved = heap.reserve(fields.size() + 2);
    if (!reserved) {
        return std::unexpected(heap_error(reserved.error()));
    }
    auto *words = ::new (reserved->bytes().data()) Word[fields.size() + 2]{};
    words[0] = detail::layout::BoxHeader::make(BoxedKind::native_record, fields.size() + 1);
    words[1] = reinterpret_cast<Word>(&definition);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        words[i + 2] = word_of(fields[i]);
    }
    return detail::publish(storage, *reserved,
                           reinterpret_cast<Word>(words) | static_cast<Word>(TermKindPrimary::boxed));
}

// Only a definition this runtime registered may be captured, with one value per field.
TermResult<void> registered(ProcessContext &context, const RecordDefinition &definition, std::size_t fields) {
    if (context.code_server().record_definition(definition.descriptor) != &definition) {
        return std::unexpected(TermError::wrong_owner);
    }
    return fields == definition.fields.size() ? TermResult<void>{} : std::unexpected(TermError::invalid_argument);
}
} // namespace

TermResult<Term> TermFactory::native_record(const RecordDefinition &definition, std::span<const Term> fields) {
    const auto owner = heap();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    if (const auto checked = registered((*owner)->owner_, definition, fields.size()); !checked) {
        return std::unexpected(checked.error());
    }
    for (const auto &field : fields) {
        if (const auto owned = (*owner)->retain(field); !owned) {
            return std::unexpected(owned.error());
        }
    }
    return runtime::record(**owner, (*owner)->storage_, definition, fields);
}

TermResult<Term> TermFactory::native_record_words(const RecordDefinition &definition, std::span<const Word> fields) {
    const auto owner = heap();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    if (const auto checked = registered((*owner)->owner_, definition, fields.size()); !checked) {
        return std::unexpected(checked.error());
    }
    for (const auto field : fields) {
        if (const auto admitted = Term::from_word(field, (*owner)->owner_); !admitted) {
            return std::unexpected(admitted.error());
        }
    }
    return runtime::record(**owner, (*owner)->storage_, definition, fields);
}

bool Term::is_native_record() const { return kind() == TermKind::native_record; }

TermResult<Term> Term::record_field(const Term &name) const {
    const auto view = detail::record_view(*this);
    if (!view) {
        return std::unexpected(view.error());
    }
    if (!name.is_atom()) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto position = detail::record_position(*view->definition, name.word());
    if (!position) {
        return std::unexpected(TermError::unknown_field);
    }
    return detail::TermAccess::child(*this, view->values[*position]);
}

TermResult<std::vector<std::pair<Term, Term>>> Term::record_fields() const {
    const auto view = detail::record_view(*this);
    if (!view) {
        return std::unexpected(view.error());
    }
    std::vector<std::pair<Term, Term>> result;
    result.reserve(view->values.size());
    for (std::size_t i = 0; i < view->values.size(); ++i) {
        auto name = detail::TermAccess::child(*this, view->definition->fields[i]);
        auto value = detail::TermAccess::child(*this, view->values[i]);
        if (!name || !value) {
            return std::unexpected(name ? value.error() : name.error());
        }
        result.emplace_back(*name, *value);
    }
    return result;
}

namespace detail {
TermResult<RecordView> record_view(const Term &value) noexcept {
    const auto object = TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::native_record) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto &cell = *reinterpret_cast<const layout::NativeRecordCell *>(object->words.data());
    return RecordView{cell.definition_, object->words.subspan(2)};
}

TermResult<std::pair<Term, Term>> record_identity(const Term &value) noexcept {
    const auto view = record_view(value);
    if (!view) {
        return std::unexpected(view.error());
    }
    auto module = TermAccess::child(value, view->definition->module);
    auto name = TermAccess::child(value, view->definition->name);
    if (!module || !name) {
        return std::unexpected(module ? name.error() : module.error());
    }
    return std::pair{*module, *name};
}

std::optional<std::size_t> record_position(const RecordDefinition &definition, Word field) noexcept {
    const auto found = std::ranges::find(definition.fields, field);
    if (found == definition.fields.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - definition.fields.begin());
}
} // namespace detail
} // namespace clause::runtime
