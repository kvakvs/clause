#pragma once
#include <atomic>
#include <clause/runtime/terms.hpp>
#include <shared_mutex>
#include <utility>
#include <vector>

// Pid and port numbers (docs/terms.md#pids-and-references, docs/ports.md#identity): one process-wide sequence of
// each kind that is never recycled, so the pid or port word of another runtime or a number never issued is
// rejected like a forged word.
namespace clause::runtime::detail {
class IdentityNumbers final {
  public:
    // Reserve the next pid or port number for this runtime; resource_limit once the payload is exhausted.
    TermResult<Word> issue_pid() { return issue(pids_, pid_sequence()); }

    TermResult<Word> issue_port() { return issue(ports_, port_sequence()); }

    // Whether this runtime issued `number`; identities of ended processes and closed ports stay valid.
    bool issued_pid(const Word number) const noexcept { return issued(pids_, number); }

    bool issued_port(const Word number) const noexcept { return issued(ports_, number); }

  private:
    // Issued numbers as ascending [first, end) runs: a single run while one runtime creates every identity.
    using Runs = std::vector<std::pair<Word, Word>>;
    // The process-wide sequences pids and ports take their numbers from.
    static std::atomic<Word> &pid_sequence() noexcept;
    static std::atomic<Word> &port_sequence() noexcept;
    // Reserve a number from `sequence` and record it in `runs`.
    TermResult<Word> issue(Runs &runs, std::atomic<Word> &sequence) const;
    // Whether `runs` holds `number`.
    bool issued(const Runs &runs, Word number) const noexcept;

    Runs pids_;
    Runs ports_;
    // Workers admit identity words while another issues a number: shared for issued(), exclusive for issue().
    mutable std::shared_mutex mutex_;
};

// The immediate pid word of a process number, and the number of a pid word.
Word pid_word(Word number) noexcept;
Word pid_number(Word word) noexcept;
// The immediate port word of a port number, and the number of a port word.
Word port_word(Word number) noexcept;
Word port_number(Word word) noexcept;
} // namespace clause::runtime::detail
