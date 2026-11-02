#include "funs.hpp"
#include "../memory/heap_object.hpp"
#include "../memory/heap_storage.hpp"
#include "term_layout.hpp"
#include "terms.hpp"
#include <clause/runtime/process_context.hpp>
#include <new>

namespace clause::runtime {
namespace {
// Keep allocation failures distinct from the configured backing ceiling.
TermError heap_error(HeapError error) {
    return error == HeapError::out_of_memory ? TermError::out_of_memory : TermError::resource_limit;
}

// The ABI word of a captured value given as a Term or as a word.
Word word_of(const Term &value) { return value.word(); }

Word word_of(Word value) { return value; }

// Publish the header, the definition word and every captured value as one immutable object.
template <typename Value>
TermResult<Term> fun_cell(ProcessHeap &heap, const std::shared_ptr<detail::HeapStorage> &storage,
                          const FunDefinition &definition, std::span<const Value> captures) {
    auto reserved = heap.reserve(captures.size() + 2);
    if (!reserved) {
        return std::unexpected(heap_error(reserved.error()));
    }
    auto *words = ::new (reserved->bytes().data()) Word[captures.size() + 2]{};
    words[0] = detail::layout::BoxHeader::make(BoxedKind::fun_closure, captures.size() + 1);
    words[1] = reinterpret_cast<Word>(&definition);
    for (std::size_t i = 0; i < captures.size(); ++i) {
        words[i + 2] = word_of(captures[i]);
    }
    return detail::publish(storage, *reserved,
                           reinterpret_cast<Word>(words) | static_cast<Word>(TermKindPrimary::boxed));
}

// Only a definition this runtime registered may be used, with exactly its captured value count.
TermResult<void> registered(ProcessContext &context, const FunDefinition &definition, std::size_t captures) {
    if (!context.code_server().owns(definition)) {
        return std::unexpected(TermError::wrong_owner);
    }
    return captures == definition.captures ? TermResult<void>{} : std::unexpected(TermError::invalid_argument);
}
} // namespace

TermResult<Term> TermFactory::fun(const FunDefinition &definition, std::span<const Term> captures) {
    const auto owner = heap();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    if (const auto checked = registered((*owner)->owner_, definition, captures.size()); !checked) {
        return std::unexpected(checked.error());
    }
    for (const auto &capture : captures) {
        if (const auto owned = (*owner)->retain(capture); !owned) {
            return std::unexpected(owned.error());
        }
    }
    return runtime::fun_cell(**owner, (*owner)->storage_, definition, captures);
}

TermResult<Term> TermFactory::fun_words(const FunDefinition &definition, std::span<const Word> captures) {
    const auto owner = heap();
    if (!owner) {
        return std::unexpected(owner.error());
    }
    if (const auto checked = registered((*owner)->owner_, definition, captures.size()); !checked) {
        return std::unexpected(checked.error());
    }
    for (const auto capture : captures) {
        if (const auto admitted = Term::from_word(capture, (*owner)->owner_); !admitted) {
            return std::unexpected(admitted.error());
        }
    }
    return runtime::fun_cell(**owner, (*owner)->storage_, definition, captures);
}

bool Term::is_function() const { return kind() == TermKind::function; }

bool Term::is_function(std::size_t arity) const {
    const auto view = detail::fun_view(*this);
    return view && view->definition->arity == arity;
}

namespace detail {
TermResult<FunView> fun_view(const Term &value) noexcept {
    const auto object = TermAccess::object(value);
    if (!object) {
        return std::unexpected(object.error());
    }
    if (object->kind != TermKind::function) {
        return std::unexpected(TermError::wrong_type);
    }
    const auto &cell = *reinterpret_cast<const layout::FunCell *>(object->words.data());
    return FunView{cell.definition_, object->words.subspan(2)};
}

namespace {
// Append one atom word of a fun's definition: quoted as the emulator printer does, or its raw spelling.
TermResult<void> fun_atom(const Term &fun, Word atom, bool quoted, TextOutput &out) {
    const auto name = TermAccess::child(fun, atom);
    if (!name) {
        return std::unexpected(name.error());
    }
    if (quoted) {
        return print_atom(*name, TermStyle::display, out);
    }
    const auto spelling = name->atom_spelling();
    if (!spelling) {
        return std::unexpected(spelling.error());
    }
    out.append(*spelling);
    return {};
}
} // namespace

// Both styles use the emulator's text (erlang:fun_to_list/1); the uniq part of a local fun is always 0, another node's
// fun prints its old index and old uniq, as OTP's.
TermResult<void> print_fun(const Term &value, TermStyle, TextOutput &out) {
    const auto view = fun_view(value);
    if (!view) {
        return std::unexpected(view.error());
    }
    const auto &definition = *view->definition;
    out.append(definition.external ? "fun " : "#Fun<");
    if (const auto module = fun_atom(value, definition.module, definition.external, out); !module) {
        return module;
    }
    if (definition.foreign) {
        out.append("." + std::to_string(definition.old_index) + "." + std::to_string(definition.old_uniq) + ">");
        return {};
    }
    if (!definition.external) {
        out.append("." + std::to_string(definition.index) + ".0>");
        return {};
    }
    out.append(":");
    if (const auto function = fun_atom(value, definition.function, true, out); !function) {
        return function;
    }
    out.append("/" + std::to_string(definition.arity));
    return {};
}
} // namespace detail
} // namespace clause::runtime
