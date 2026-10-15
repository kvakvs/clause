#include "terms.hpp"
#include <clause/runtime/atoms.hpp>
#include <clause/runtime/process_context.hpp>

namespace clause::runtime {
TermFactory::TermFactory(ProcessContext &context, DiagnosticSink sink) noexcept
    : lifetime_(context.lifetime()), sink_(sink), atoms_(&context.atom_storage()), heap_(&context.heap()) {}

TermResult<Term> TermFactory::unavailable(std::string_view operation) const noexcept {
    if (const auto owner = heap(); !owner) {
        return std::unexpected(owner.error());
    }
    return deferred_service<TermError>(abi::v1::FeatureId::term_services, operation, sink_);
}

TermResult<Term> TermFactory::atom(std::string_view spelling) {
    if (const auto owner = heap(); !owner) {
        return std::unexpected(owner.error());
    }
    return atoms_->intern(spelling);
}

TermResult<Term> TermFactory::boolean(bool value) { return atom(value ? "true" : "false"); }

TermResult<Term> TermFactory::function(const FunctionIdentity &) { return unavailable("TermFactory::function"); }
} // namespace clause::runtime
