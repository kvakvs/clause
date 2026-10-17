#include "../memory/heap_object.hpp"
#include <clause/runtime/terms.hpp>
#include <new>
#include <stdexcept>

namespace clause::runtime {
namespace {
// The decoded header supplies both shape and extent before any field load.
TermResult<detail::HeapObject> container(const Term &value, TermKind expected) {
    const auto object = detail::TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != expected) {
        return std::unexpected(TermError::wrong_type);
    }
    return *object;
}

// Cons words are headerless and have exactly two published fields.
TermResult<Term> cons_field(const Term &value, std::size_t index) {
    const auto object = container(value, TermKind::list);
    if (!object) {
        return std::unexpected(object.error());
    }
    return detail::TermAccess::child(value, object->words[index]);
}
} // namespace

bool Term::is_tuple() const {
    const auto found = kind();
    return found == TermKind::tuple || found == TermKind::empty_tuple;
}

bool Term::is_nil() const { return kind() == TermKind::empty_list; }

bool Term::is_cons() const { return kind() == TermKind::list; }

bool Term::is_list() const { return is_nil() || is_cons(); }

TermResult<Term> Term::head() const { return cons_field(*this, 0); }

TermResult<Term> Term::tail() const { return cons_field(*this, 1); }

TermResult<std::size_t> Term::tuple_size() const {
    if (kind() == TermKind::empty_tuple) {
        return 0;
    }
    return container(*this, TermKind::tuple).transform([](const auto &object) { return object.count; });
}

TermResult<Term> Term::tuple_element(std::size_t index) const {
    const auto size = tuple_size();
    if (!size) {
        return std::unexpected(size.error());
    }
    if (index >= *size) {
        return std::unexpected(TermError::out_of_range);
    }
    const auto object = detail::TermAccess::object(*this).value();
    return detail::TermAccess::child(*this, object.words[index + 1]);
}

TermResult<std::vector<Term>> Term::tuple_elements() const {
    const auto size = tuple_size();
    if (!size) {
        return std::unexpected(size.error());
    }
    try {
        std::vector<Term> result;
        result.reserve(*size);
        for (std::size_t i = 0; i < *size; ++i) {
            const auto value = tuple_element(i);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.push_back(*value);
        }
        return result;
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    } catch (const std::length_error &) {
        return std::unexpected(TermError::resource_limit);
    }
}

TermResult<std::size_t> Term::list_length() const {
    if (const auto checked = detail::TermAccess::validate(*this); !checked) {
        return std::unexpected(checked.error());
    }
    if (!is_list()) {
        return std::unexpected(TermError::wrong_type);
    }
    auto current = *this;
    std::size_t count = 0;
    while (current.is_cons()) {
        ++count;
        const auto next = current.tail();
        if (!next) {
            return std::unexpected(next.error());
        }
        current = *next;
    }
    return current.is_nil() ? TermResult<std::size_t>{count} : std::unexpected(TermError::improper_list);
}

TermResult<bool> Term::is_proper_list() const {
    const auto length = list_length();
    if (length) {
        return true;
    }
    if (length.error() == TermError::wrong_type || length.error() == TermError::improper_list) {
        return false;
    }
    return std::unexpected(length.error());
}
} // namespace clause::runtime
