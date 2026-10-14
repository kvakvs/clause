#pragma once
#include <clause/abi/modules.hpp>
#include <clause/runtime/base_types.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace clause::runtime {
class ProcessContext;
struct CallFailure;
} // namespace clause::runtime

// Diagnostic call tracing, enabled by the CLAUSE_CALL_TRACE=FILE environment variable. Added for call tracing:
// it logs the calls a program's own modules make into library modules, with bounded arguments and results, so a
// run can be compared with the same program traced on Erlang/OTP.
namespace clause::runtime::detail {
// The call trace of one process, kept by its stack; only that process's worker touches it.
class CallTrace final {
  public:
    // How a frame ended: its result, or a failure being unwound.
    struct Outcome final {
        Word result;
        bool failed;
    };

    // A tracer for a new process, or null when tracing is off.
    static std::unique_ptr<CallTrace> create(ProcessContext &owner);

    explicit CallTrace(ProcessContext &owner) noexcept : owner_(owner) {}

    // `callee` entered at frame offset `frame` (none for a builtin) from the frame `caller`; `replaced` is the frame
    // a tail call released, or none. Logs a boundary call with its arguments.
    void enter(const abi::v1::FrameDescriptor *caller, const abi::v1::FrameDescriptor &callee, std::size_t frame,
               std::size_t replaced, const Word *arguments) noexcept;
    // A builtin entered through enter() with frame none finished with `result`, failed, or trapped to continue later.
    void builtin_done(Word result, bool failed, bool trapped) noexcept;
    // The frame at offset `frame` returns or unwinds a failure.
    void leave(std::size_t frame, Outcome outcome) noexcept;
    // An exception or failure was recorded while `running` was the innermost frame.
    void failure(const abi::v1::FrameDescriptor *running, const CallFailure &failure) noexcept;

  private:
    // An open boundary call: its frame offset (none for a builtin) and its name.
    struct Open final {
        std::size_t frame;
        std::string name;
    };

    // Write one line indented by the open boundary calls.
    void line(std::string_view text) const noexcept;
    // A term as text, bounded in depth and width the same way the OTP-side tracer prints it.
    std::string text(Word word) const noexcept;
    // Drop the open calls a frame at offset `frame` released; true when `replaced` continues an open boundary call.
    bool release(std::size_t frame, std::size_t replaced);
    // Log the first entry of `callee` under CLAUSE_CALL_TRACE_FIRST.
    void note_first(const abi::v1::FrameDescriptor &callee) const;

    ProcessContext &owner_;
    // Boundary calls not returned yet, innermost last.
    std::vector<Open> open_;
    // Trace lines lost to a failure while tracing; tracing never fails the program.
    std::size_t dropped_ = 0;
};

// Whether CLAUSE_CALL_TRACE names a trace file.
bool call_trace_enabled() noexcept;
} // namespace clause::runtime::detail
