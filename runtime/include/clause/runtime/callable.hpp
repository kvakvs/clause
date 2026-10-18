#pragma once
#include "terms.hpp"
#include <array>
#include <clause/abi/calls.hpp>
#include <clause/abi/modules.hpp>
#include <clause/abi/status.hpp>
#include <functional>
#include <map>
#include <typeindex>

namespace clause::runtime {
// Separate invocation failures from registration and lookup failures.
enum class CallError : std::uint8_t {
    bad_arity,
    argument_type_mismatch,
    wrong_owner,
    expired_context,
    resource_limit,
    native_exception,
    not_implemented,
    diagnostic_failure,
    erlang_exception,
    runtime_failure,
    halted,
    // Ended by an exit signal (exit/2, a link): unwinds past every catch; the value is the exit reason.
    exited
};

// Generated frames live when an Erlang exception was raised, innermost first (BEAM's raw StackTrace).
struct StackTrace final {
    // The most frames a trace holds: OTP's largest backtrace_depth; backtrace_depth() picks how many are captured.
    static constexpr std::size_t limit = 64;
    std::array<const abi::v1::FrameDescriptor *, limit> frames = {};
    std::size_t depth = 0;
};

// The frames stack traces keep, BEAM's backtrace_depth: 8 until system_flag(backtrace_depth, N) changes it, for
// every runtime of the program (plan 21 step LIB9A).
std::size_t backtrace_depth() noexcept;
// Set backtrace_depth, at most StackTrace::limit; returns the previous depth.
std::size_t set_backtrace_depth(std::size_t depth) noexcept;

struct CallFailure final {
    // Preserve the operation failure without manufacturing an Erlang exception term.
    CallError code;
    // Identify an argument failure, or leave empty for a result/body failure.
    std::optional<std::size_t> argument = {};
    // Retain explicit term validation errors for host callers.
    std::optional<TermError> term_error = {};
    // Prevent nested checked calls from reporting the same unavailable body again.
    bool reported = false;
    // Retain exact infrastructure status separately from Erlang exception reasons.
    std::optional<abi::v1::Status> status = {};
    // The typed or raised Erlang reason; raised reasons also select the class (error, exit or throw).
    std::optional<abi::v1::ErrorReason> reason = {};
    // Own an admitted badmatch or {badarg,Value} payload, or the erlang:halt/1 slogan string.
    std::optional<Term> value = {};
    // Exit status requested by erlang:halt/0,1; the program stops once the entry call unwinds.
    std::optional<int> halt_status = {};
    // Frames captured when an Erlang exception is recorded; host failures have none.
    StackTrace trace = {};
    // The erlang:error/2,3 argument list shown in the top frame instead of its arity (a process root).
    std::optional<Term> arguments = {};
    // A complete stack trace term from erlang:raise/3 or a re-raise; replaces `trace` (a process root).
    std::optional<Term> stack = {};
};

template <typename Value> using CallResult = std::expected<Value, CallFailure>;
// Generic entries always receive exactly their registered Erlang arity, with no conversions.
using Callable = std::function<CallResult<Term>(ProcessContext &, std::span<const Term>)>;

enum class RegistryError : std::uint8_t { invalid_entry, duplicate_key, function_not_found, frozen, resource_limit };
template <typename Value> using RegistryResult = std::expected<Value, RegistryError>;

struct FunctionKey final {
    // Own exact spelling independently of draft buffers; future atom bindings require runtime initialization.
    std::string name;
    // Exclude the leading process context from Erlang arity and type identity.
    std::size_t arity;
    // Generic keys contain one typeid(Term) per argument; native extensions must match exactly.
    std::vector<std::type_index> argument_types;
    // Order canonical signatures without normalizing names or native type identities.
    auto operator<=>(const FunctionKey &) const = default;
};

// Build one registry privately, then freeze that same owner at module publication.
class ModuleRegistry final {
  public:
    // Start a mutable, empty draft; its address stays stable through publication.
    ModuleRegistry() = default;
    ModuleRegistry(const ModuleRegistry &) = delete;
    ModuleRegistry &operator=(const ModuleRegistry &) = delete;
    ~ModuleRegistry() = default;
    // Add only all-Term entries; invalid targets, arities above 255 and duplicate signatures fail.
    RegistryResult<void> add(std::string_view name, std::size_t arity, Callable &&target);
    // Borrow the exact generic target; callers retain its registry/module through use.
    RegistryResult<const Callable *> find(std::string_view name, std::size_t arity) const;
    // Copy canonical signatures in stable name/arity order.
    std::vector<FunctionKey> keys() const;
    // Observe publication without exposing mutable entries.
    bool frozen() const noexcept;

  private:
    friend class CodeServer;
    // Reject subsequent additions, including through surviving draft aliases.
    void freeze() noexcept;
    // Share one stored callable instance across every lookup of its exact key.
    std::map<FunctionKey, Callable> entries_;
    // Change only after successful publication; draft mutation requires host serialization.
    bool frozen_ = false;
};
} // namespace clause::runtime
