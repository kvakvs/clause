#pragma once

// The messages of one process (docs/processes.md#messages): sends append to its signal inbox; receive moves arrived
// messages behind its message queue and scans that queue from a saved position.
#include "terms.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <list>
#include <optional>

namespace clause::runtime {
class Mailbox final {
  public:
    Mailbox(const Mailbox &) = delete;
    Mailbox &operator=(const Mailbox &) = delete;
    Mailbox(Mailbox &&) = delete;
    Mailbox &operator=(Mailbox &&) = delete;
    ~Mailbox() = default;

    // Append a message already copied into the owner's heap to the signal inbox; allocation failure throws.
    void deliver(Word message);
    // The next message the current receive has not examined, after moving arrived messages behind the queue; none
    // when every message has been examined.
    std::optional<Word> peek() noexcept;
    // Leave the examined message in place and move on to the next one.
    void skip() noexcept;
    // Remove the examined message, which a receive clause matched, and start the next receive at the oldest message;
    // the removed message, none when every message was examined.
    std::optional<Word> take() noexcept;
    // Start the next receive at the oldest message, as a receive that timed out does.
    void restart() noexcept;

    // Remove the oldest message `match` accepts; false when none does. The current receive's position moves past a
    // removed message.
    template <typename Match> bool remove(Match &&match) {
        for (auto *messages : {&queue_, &inbox_}) {
            const auto found = std::ranges::find_if(*messages, match);
            if (found != messages->end()) {
                position_ = found == position_ ? std::next(found) : position_;
                messages->erase(found);
                return true;
            }
        }
        return false;
    }

    // When the current receive's timeout expires; none for no timeout yet or infinity. take and restart clear it.
    std::optional<std::chrono::steady_clock::time_point> deadline() const noexcept { return deadline_; }

    void set_deadline(std::chrono::steady_clock::time_point deadline) noexcept { deadline_ = deadline; }

    // Whether a message arrived that the current receive has not examined.
    bool unexamined() const noexcept { return !inbox_.empty() || position_ != queue_.end(); }

    // Count the messages in the inbox and the queue.
    std::size_t size() const noexcept { return inbox_.size() + queue_.size(); }

    // Visit every message word so a collector can rewrite it in place: messages are roots until received.
    template <typename Visitor> void visit(Visitor &&visit) {
        for (auto &word : inbox_) {
            visit(word);
        }
        for (auto &word : queue_) {
            visit(word);
        }
    }

  private:
    friend class ProcessContext;
    // Start empty; the owning context holds it.
    Mailbox() = default;

    // Messages sent but not yet seen by a receive, oldest first.
    std::list<Word> inbox_;
    // Messages a receive has seen, oldest first; unmatched ones stay in order.
    std::list<Word> queue_;
    // The next message of the queue the current receive examines; end() when it has examined all.
    std::list<Word>::iterator position_ = queue_.end();
    // The current receive's timeout, set when it first waits with a finite one.
    std::optional<std::chrono::steady_clock::time_point> deadline_;
};
} // namespace clause::runtime
