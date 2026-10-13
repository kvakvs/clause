#include "storage.hpp"

namespace clause::ast {
namespace detail {
const OriginTable &source_table(const Storage &storage, const NodeSource &source) {
    const auto &table = storage.origins.get(source.form);
    if (source.begin > source.end || source.end > table.tokens.size()) {
        throw std::out_of_range("AST source extent");
    }
    if (source.anchor < source.begin || source.anchor > source.end) {
        throw std::out_of_range("AST source anchor");
    }
    if (source.begin != source.end && source.anchor == source.end) {
        throw std::out_of_range("AST anchor must belong to its extent");
    }
    return table;
}
} // namespace detail

Module::Module() : storage_(std::make_unique<detail::Storage>()) {}

Module::~Module() = default;
Module::Module(Module &&) noexcept = default;
Module &Module::operator=(Module &&) noexcept = default;

const detail::Storage &Module::storage() const {
    if (!storage_) {
        throw std::logic_error("moved-from AST module");
    }
    return *storage_;
}

std::span<const FormId> Module::forms() const { return storage().roots; }

const Form &Module::form(const FormId &id) const { return storage().forms.get(id); }

const Expression &Module::expression(const ExprId &id) const { return storage().expressions.get(id); }

std::size_t Module::expression_count() const { return storage().expressions.size(); }

const Expression &Module::expression_at(const std::size_t index) const { return storage().expressions.at(index); }

const LiteralTerm &Module::term(const TermId &id) const { return storage().terms.get(id); }

std::size_t Module::term_count() const { return storage().terms.size(); }

const TypeSyntax &Module::type(const TypeId &id) const { return storage().types.get(id); }

std::size_t Module::type_count() const { return storage().types.size(); }

const PatternSyntax &Module::pattern(const PatternSyntaxId &id) const { return storage().patterns.get(id); }

std::size_t Module::pattern_count() const { return storage().patterns.size(); }

FeatureSnapshot Module::features(const FormId &id) const {
    return detail::source_table(storage(), form(id).source).features;
}

FeatureSnapshot Module::features() const { return storage().features; }

const TokenOrigin &Module::anchor(const NodeSource &source) const {
    const auto &table = detail::source_table(storage(), source);
    return source.anchor == table.tokens.size() ? table.eof : table.tokens[source.anchor];
}

std::span<const TokenOrigin> Module::extent(const NodeSource &source) const {
    const auto &table = detail::source_table(storage(), source);
    return std::span(table.tokens).subspan(source.begin, source.end - source.begin);
}
} // namespace clause::ast
