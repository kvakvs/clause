#pragma once
#include <clause/abi/immediate_services.hpp>
#include <clause/abi/status.hpp>
#include <clause/runtime/process_context.hpp>
#include <expected>
#include <optional>

// The operations of the immediate service on terms any process may read, for runtime callers outside generated code
// (match specifications, plan 21 step LIB11; docs/runtime.md#match-specifications).
namespace clause::runtime::detail {
// The result word of `operation` on `left` (and `right` for binary operations), allocated in the caller's heap; none
// when the operation rejects its arguments as a guard BIF would (badarg, badarith, system_limit); the status of an
// infrastructure failure otherwise.
std::expected<std::optional<Word>, abi::v1::Status>
immediate_terms(ProcessContext &context, abi::v1::ImmediateOperation operation, const Term &left, const Term &right);
} // namespace clause::runtime::detail
