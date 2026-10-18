#pragma once

// The links, monitors, registered name and exit trapping of one process (docs/processes.md#links, #monitors,
// #registered-names): the pids of the processes it is linked to, in the order the links were made, the monitors it
// holds and those held on it, its name, and whether exit signals reach it as messages. Links, monitors and the name
// change only under the executor's lock, also while the process runs; exit trapping only by the process itself.
#include "terms.hpp"
#include <memory>

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace clause::runtime {
namespace detail {
class MessageBuffer;
} // namespace detail

class Signals final {
  public:
    Signals(const Signals &) = delete;
    Signals &operator=(const Signals &) = delete;
    Signals(Signals &&) = delete;
    Signals &operator=(Signals &&) = delete;
    ~Signals() = default;

    // Whether exit signals become {'EXIT', From, Reason} messages (process_flag(trap_exit, Bool)).
    bool trap_exit() const noexcept { return trap_exit_; }

    // Set exit trapping; returns the previous setting.
    bool set_trap_exit(bool trap) noexcept { return std::exchange(trap_exit_, trap); }

    // process_flag(save_calls, N), kept for reading back only: Clause saves no calls (plan 21 step LIB9A). Changed
    // under the executor's lock when another process sets it.
    std::size_t set_save_calls(std::size_t calls) noexcept { return std::exchange(save_calls_, calls); }

    // Link to the process of `pid`; nothing when already linked. Allocation failure throws.
    void link(Word pid) {
        if (!std::ranges::contains(links_, pid)) {
            links_.push_back(pid);
        }
    }

    // Remove the link to the process of `pid`, if there is one.
    void unlink(Word pid) noexcept { std::erase(links_, pid); }

    // The pids of the linked processes, oldest link first.
    const std::vector<Word> &links() const noexcept { return links_; }

    // Remove every link and return them, as the process ends.
    std::vector<Word> take_links() noexcept { return std::exchange(links_, {}); }

    // The other side of a monitor: its pid, and the registered name (an atom word) the monitor was made with, or 0.
    struct Monitor {
        Word pid;
        Word name = 0;
        // monitor/3's {tag, Tag}: the first element of the 'DOWN' message instead of 'DOWN' (step 41G2).
        std::shared_ptr<const detail::MessageBuffer> tag = nullptr;
    };

    // The monitors of one side, by reference. References order by creation.
    using Monitors = std::map<ReferenceIdentity, Monitor>;

    // Monitor the process of `pid` with `reference` (monitor/2). Allocation failure throws.
    void monitor(const ReferenceIdentity &reference, Word pid) { monitors_.emplace(reference, Monitor{pid}); }

    // Stop the monitor `reference` this process holds; the monitored pid, or none when it is not active.
    std::optional<Word> demonitor(const ReferenceIdentity &reference) noexcept {
        const auto node = monitors_.extract(reference);
        return node ? std::optional{node.mapped().pid} : std::nullopt;
    }

    // Record that `watcher` monitors this process with `reference`. Allocation failure throws.
    void watch(const ReferenceIdentity &reference, const Monitor &watcher) { watchers_.emplace(reference, watcher); }

    // Forget the monitor `reference` held on this process.
    void unwatch(const ReferenceIdentity &reference) noexcept { watchers_.erase(reference); }

    // Remove and return the monitors this process holds, as it ends; allocation failure throws.
    Monitors take_monitors() { return std::exchange(monitors_, {}); }

    // The monitors held on this process.
    const Monitors &watchers() const noexcept { return watchers_; }

    // The monitors this process holds (process_info/2 monitors, step 41G1).
    const Monitors &monitors() const noexcept { return monitors_; }

    // The group leader's pid word, or 0 for the process itself (group_leader/0,2, step 41G1).
    Word group_leader() const noexcept { return group_leader_; }

    void set_group_leader(Word pid) noexcept { group_leader_ = pid; }

    // Remove and return the monitors held on this process, as it ends: each gets a 'DOWN' message.
    Monitors take_watchers() { return std::exchange(watchers_, {}); }

    // The registered name of the process, an atom word, or 0 for none (register/2).
    Word name() const noexcept { return name_; }

    // Register or (with 0) unregister the process's name.
    void set_name(Word name) noexcept { name_ = name; }

  private:
    friend class ProcessContext;
    // Start without links, not trapping exits; the owning context holds it.
    Signals() = default;

    // Pid words of the linked processes, oldest link first, so exit signals go out in a stable order.
    std::vector<Word> links_;
    // Monitors this process holds: reference to the monitored pid.
    Monitors monitors_;
    // Monitors held on this process: reference to the monitoring pid.
    Monitors watchers_;
    // The registered name, an atom word, or 0.
    Word name_ = 0;
    // Turns exit signals other than kill into messages.
    bool trap_exit_ = false;
    // The group leader inherited from the spawning process; 0 means the process leads itself. Added for 41G1.
    Word group_leader_ = 0;
    // process_flag(save_calls, N); nothing is saved (plan 21 step LIB9A).
    std::size_t save_calls_ = 0;
};
} // namespace clause::runtime
