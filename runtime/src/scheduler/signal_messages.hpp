#pragma once
#include "executor.hpp"
#include <string_view>
#include <utility>

// Terms of exit signals and monitor messages, shared by process signals (signals.cpp) and ports (ports.cpp).
namespace clause::runtime::detail {
// Whether `term` is the atom spelled `name`.
bool is_atom(const Term &term, std::string_view name);
// The atom `name`; a full atom table fails like exhausted memory (throws std::bad_alloc).
Term atom(ProcessContext &process, AtomName name);
// {'EXIT', From, Reason} built in the heap of `process`; From is a pid or a port.
TermResult<Term> exit_message(ProcessContext &process, Word from, const Term &reason);
// {'DOWN', Ref, Type, Item, Reason} built in the heap of `process`: Type is port for a monitored port, else process;
// Item is the pid or port, or {Name, nonode@nohost} for a monitor made with a registered name.
TermResult<Term> down_message(ProcessContext &process, const ReferenceIdentity &reference, const Signals::Monitor &item,
                              const Term &reason);

// Makes a process the running one of this thread for a signal operation of a builtin, which may run outside run().
class Executor::Running final {
  public:
    // Make `process` this thread's running one until the scope ends.
    explicit Running(ProcessContext &process) noexcept : saved_(std::exchange(running_, &process)) {}

    ~Running() { running_ = saved_; }

    Running(const Running &) = delete;
    Running &operator=(const Running &) = delete;
    Running(Running &&) = delete;
    Running &operator=(Running &&) = delete;

  private:
    // The running process before the scope: itself on a worker, none for a host invocation.
    ProcessContext *saved_;
};
} // namespace clause::runtime::detail
