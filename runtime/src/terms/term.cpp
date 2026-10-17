#include "../memory/heap_object.hpp"
#include "../memory/heap_storage.hpp"
#include "atom_table.hpp"
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/process_context.hpp>

namespace clause::runtime {
TermResult<Term> Term::from_word(Word value) noexcept {
    const auto kind = classify_immediate(value);
    if (!kind) {
        return std::unexpected(kind.error());
    }
    if (*kind != TermKind::smallint && *kind != TermKind::empty_tuple && *kind != TermKind::empty_list) {
        return std::unexpected(TermError::not_implemented);
    }
    Term result;
    result.value_ = value;
    return result;
}

TermResult<Term> Term::from_word(Word value, ProcessContext &context) noexcept {
    return detail::TermAccess::admit(value, *context.heap().storage_);
}

void Term::rebind(Word value) noexcept { value_ = value; }

// The record of an atom term, read from the process-wide atom table (one-word terms).
static TermResult<const AtomValue *> atom_record(Word value) noexcept {
    if (TermTag{value}.get_kind() != TermKind::atom) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto *record = detail::atom_table::find(value);
    if (!record) {
        return std::unexpected(TermError::expired_context);
    }
    return record;
}

bool Term::is_atom() const { return TermTag{value_}.get_kind() == TermKind::atom; }

TermResult<AtomId> Term::atom_id() const {
    if (!is_atom()) {
        return std::unexpected(TermError::wrong_type);
    }
    return value_ >> abi::v1::atom_tag_bits;
}

TermResult<std::string> Term::atom_utf8() const {
    return atom_spelling().and_then([](std::string_view spelling) -> TermResult<std::string> {
        try {
            return std::string(spelling);
        } catch (const std::bad_alloc &) {
            return std::unexpected(TermError::resource_limit);
        }
    });
}

TermResult<bool> Term::boolean_value() const {
    if (value_ == abi::v1::true_atom || value_ == abi::v1::false_atom) {
        return value_ == abi::v1::true_atom;
    }
    return std::unexpected(TermError::wrong_type);
}

bool Term::is_boolean() const { return boolean_value().has_value(); }

TermResult<std::string_view> Term::atom_spelling() const noexcept {
    return atom_record(value_).transform([](const AtomValue *record) { return std::string_view{record->spelling}; });
}

Word Term::word() const noexcept { return value_; }

TermKind Term::kind() const {
    const auto kind = TermTag{value_}.get_kind();
    if (kind == TermKind::boxed || kind == TermKind::list) {
        const auto object = detail::TermAccess::object(*this);
        return object ? object->kind : TermKind::invalid;
    }
    return kind;
}

} // namespace clause::runtime
