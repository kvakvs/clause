#pragma once
#include <clause/compiler/ast/forms.hpp>
#include <clause/compiler/features.hpp>
#include <span>

namespace clause::ast {
namespace detail {
struct Storage;
}
class Builder;

// Move-only syntax owner; all consumer access is const and validates handles.
class Module {
  public:
    // Create an empty AST without any runtime or preprocessor dependency.
    Module();
    ~Module();
    Module(Module &&) noexcept;
    Module &operator=(Module &&) noexcept;
    Module(const Module &) = delete;
    Module &operator=(const Module &) = delete;
    // Inspect committed roots or category-specific nodes; invalid/stale IDs throw.
    std::span<const FormId> forms() const;
    const Form &form(const FormId &id) const;
    const Expression &expression(const ExprId &id) const;
    std::size_t expression_count() const;
    // The expression at an arena position below expression_count(): a reference that a later parse of the same
    // source resolves to the same expression. Added for the inference cache (58V).
    const Expression &expression_at(std::size_t index) const;
    const PatternSyntax &pattern(const PatternSyntaxId &id) const;
    std::size_t pattern_count() const;
    const LiteralTerm &term(const TermId &id) const;
    std::size_t term_count() const;
    const TypeSyntax &type(const TypeId &id) const;
    std::size_t type_count() const;
    // Inspect immutable per-form snapshots and the final module feature context.
    FeatureSnapshot features(const FormId &id) const;
    FeatureSnapshot features() const;
    // Resolve owned origins, including explicit EOF anchors for empty extents.
    const TokenOrigin &anchor(const NodeSource &source) const;
    std::span<const TokenOrigin> extent(const NodeSource &source) const;

    // Dispatch to exhaustive typed visitors without exposing mutable arena storage.
    template <typename Visitor> decltype(auto) visit(const FormId &id, Visitor &&visitor) const {
        return std::visit(std::forward<Visitor>(visitor), form(id).value);
    }

    template <typename Visitor> decltype(auto) visit(const ExprId &id, Visitor &&visitor) const {
        return std::visit(std::forward<Visitor>(visitor), expression(id).value);
    }

    template <typename Visitor> decltype(auto) visit(const TermId &id, Visitor &&visitor) const {
        return std::visit(std::forward<Visitor>(visitor), term(id).value);
    }

    template <typename Visitor> decltype(auto) visit(const TypeId &id, Visitor &&visitor) const {
        return std::visit(std::forward<Visitor>(visitor), type(id).value);
    }

    template <typename Visitor> decltype(auto) visit(const PatternSyntaxId &id, Visitor &&visitor) const {
        return std::visit(std::forward<Visitor>(visitor), pattern(id).value);
    }

  private:
    friend class Builder;
    // Stable heap storage preserves handle ownership across module moves.
    std::unique_ptr<detail::Storage> storage_;
    // Reject access to a moved-from module instead of dereferencing null storage.
    const detail::Storage &storage() const;
};
} // namespace clause::ast
