#pragma once
#include <clause/runtime/process_context.hpp>
#include <string_view>

// Why processes end (docs/processes.md#exits): their exit reasons and the error reports of crashed processes.
namespace clause::runtime::detail {
// The exit reason of an ended process, built in its heap: normal after a return, the reason of exit/1,
// {Reason, Stack} for an error and {{nocatch, Value}, Stack} for an uncaught throw.
TermResult<Term> exit_reason(ProcessContext &process);
// Write OTP's error report on stderr for a process that ended with an error or an uncaught throw; a return or an
// exit writes nothing.
void report_exit(ProcessContext &process) noexcept;
// Write the error report of a runtime message `text` (ending in a newline) on the process's standard output, as OTP's
// erts_send_error_to_logger reaches it through the logger: the legacy header, the text, then two newlines (plan 30
// step DIST2b).
void report_error(ProcessContext &process, std::string_view text);
} // namespace clause::runtime::detail
