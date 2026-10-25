#pragma once

// The messages of one process (docs/processes.md#messages): sends append to its signal inbox; receive moves arrived
// messages behind its message queue and scans that queue from a saved position.
#include "terms.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <utility>

namespace clause::runtime {
namespace detail {
class MessageBuffer;

// Something lock-free readers may still reach after it was retired: freed once every worker passed a slice boundary
// after `stamp` (quiescent reclamation). Retired ones form a list through `next`.
struct Reclaimable {
    Reclaimable() = default;
    Reclaimable(const Reclaimable &) = delete;
    Reclaimable &operator=(const Reclaimable &) = delete;
    Reclaimable(Reclaimable &&) = delete;
    Reclaimable &operator=(Reclaimable &&) = delete;
    virtual ~Reclaimable() = default;

    std::uint64_t stamp = 0;
    Reclaimable *next = nullptr;
};

// Messages other processes sent, waiting in buffers outside the receiver's heap until the receiver takes them into
// it (arrival buffers): senders push without any lock, the receiver takes them all at once.
class Arrivals final : public Reclaimable {
  public:
    using Buffer = std::shared_ptr<const MessageBuffer>;

    // One waiting message: a buffer, an immediate or literal root, or a small graph copied right behind the message
    // in its one allocation (arrival messages).
    struct Message {
        Message *next = nullptr;
        Buffer buffer;
        Word root = 0;
        std::size_t words = 0;

        // The words of a graph copied behind the message.
        std::span<Word> graph() noexcept { return {reinterpret_cast<Word *>(this + 1), words}; }

        // A message with room for `words` graph words behind it. Allocation failure throws.
        static Message *make(std::size_t words) {
            auto *message = ::new (::operator new(sizeof(Message) + (words * sizeof(Word)))) Message{};
            message->words = words;
            return message;
        }

        static void destroy(Message *message) noexcept {
            message->~Message();
            ::operator delete(message);
        }
    };

    // Owns a message until it is posted.
    class Owned final {
      public:
        explicit Owned(Message *message) noexcept : message_(message) {}

        Owned(Owned &&other) noexcept : message_(std::exchange(other.message_, nullptr)) {}

        Owned(const Owned &) = delete;
        Owned &operator=(const Owned &) = delete;
        Owned &operator=(Owned &&) = delete;

        ~Owned() {
            if (message_ != nullptr) {
                Message::destroy(message_);
            }
        }

        Message *operator->() const noexcept { return message_; }

        // Hand the message over; the handle owns nothing afterwards.
        Message *release() noexcept { return std::exchange(message_, nullptr); }

      private:
        Message *message_;
    };

    Arrivals() = default;
    Arrivals(const Arrivals &) = delete;
    Arrivals &operator=(const Arrivals &) = delete;
    Arrivals(Arrivals &&) = delete;
    Arrivals &operator=(Arrivals &&) = delete;

    ~Arrivals() override { drop(); }

    // Append a message; true when the receiver was parked, so the sender must wake it.
    bool post(Owned &&owned) noexcept {
        push(owned.release());
        // Sequentially consistent with park(): either the receiver sees this message or this sender sees it parked.
        // Read before claiming, so senders to a receiver that is not parked write nothing more (arrival flags).
        return parked_.load() && parked_.exchange(false);
    }

    // Append a buffered message. Allocation failure throws.
    bool post(Buffer buffer) {
        auto message = Owned(Message::make(0));
        message->buffer = std::move(buffer);
        return post(std::move(message));
    }

    // Call each(message) on at most `limit` waiting messages, oldest first, removing them, until it returns false;
    // then every waiting message is dropped. By the receiver, or under the executor's lock while it does not run.
    template <typename Each> void take(Each each, std::size_t limit) noexcept {
        if (taken_ == nullptr) {
            // Senders push the newest first: reversed, the batch is oldest first.
            for (auto *message = head_.exchange(nullptr, std::memory_order_acquire); message != nullptr;) {
                auto *next = message->next;
                message->next = taken_;
                taken_ = message;
                message = next;
            }
        }
        for (; taken_ != nullptr && limit != 0; --limit) {
            const bool going = each(*taken_);
            Message::destroy(std::exchange(taken_, taken_->next));
            if (!going) {
                drop();
                return;
            }
        }
    }

    // Drop every waiting message.
    void drop() noexcept {
        while (taken_ != nullptr) {
            Message::destroy(std::exchange(taken_, taken_->next));
        }
        for (auto *message = head_.exchange(nullptr, std::memory_order_acquire); message != nullptr;) {
            Message::destroy(std::exchange(message, message->next));
        }
    }

    // Whether messages wait; by the receiver, or under the executor's lock while it does not run.
    bool pending() const noexcept { return taken_ != nullptr || head_.load(std::memory_order_acquire) != nullptr; }

    // Mark the receiver parked unless messages wait; false when they do. Under the executor's lock.
    bool park() noexcept {
        parked_.store(true);
        if (taken_ == nullptr && head_.load() == nullptr) {
            return true;
        }
        // A sender that already claimed the wake finds the receiver not parked and does nothing.
        parked_.store(false);
        return false;
    }

    // The receiver was woken another way: a post no longer needs to wake it.
    void unpark() noexcept { parked_.store(false, std::memory_order_relaxed); }

    // Whether the receiver runs a slice on a worker, set under the executor's lock; a sender reads it without that
    // lock to choose how to deliver, and either answer of a race is correct.
    bool running() const noexcept { return running_.load(std::memory_order_relaxed); }

    void set_running(bool running) noexcept { running_.store(running, std::memory_order_relaxed); }

  private:
    // Push a message onto the senders' list, which owns it from then on (copy.cpp).
    void push(Message *message) noexcept;

    // The newest message senders pushed, or null.
    std::atomic<Message *> head_{nullptr};
    // Messages taken from the senders' list and not yet copied, oldest first: the receiver takes them in batches
    // (arrival batches).
    Message *taken_ = nullptr;
    // Set while the receiver waits parked for a message, so the next post wakes it.
    std::atomic<bool> parked_{false};
    // A copy of the executor's running state of the receiver, readable without its lock.
    std::atomic<bool> running_{false};
};

// Spare list nodes of one mailbox, which its next messages reuse instead of the C++ heap (pooled message nodes). Only
// whoever may change the mailbox's lists reaches it: the owner, or a sender under the executor's lock while the owner
// does not run.
class NodePool final {
  public:
    NodePool() = default;
    NodePool(const NodePool &) = delete;
    NodePool &operator=(const NodePool &) = delete;
    NodePool(NodePool &&) = delete;
    NodePool &operator=(NodePool &&) = delete;

    ~NodePool() {
        while (spare_ != nullptr) {
            ::operator delete(std::exchange(spare_, spare_->next));
        }
    }

    // A block of `bytes`: an embedded one when it fits and one is free, else a spare one when it has the size spares
    // have. Allocation failure throws.
    void *take(std::size_t bytes) {
        if (bytes <= EMBEDDED_BYTES && free_embedded_ != 0) {
            const auto slot = static_cast<std::size_t>(std::countr_zero(free_embedded_));
            free_embedded_ &= free_embedded_ - 1;
            return &embedded_[slot * EMBEDDED_BYTES];
        }
        if (spare_ == nullptr || bytes != bytes_) {
            return ::operator new(bytes);
        }
        --count_;
        return std::exchange(spare_, spare_->next);
    }

    // Free an embedded block again; keep another block of `bytes` as a spare, or free it once enough are kept or its
    // size differs.
    void give(void *block, std::size_t bytes) noexcept {
        const auto *byte = static_cast<const std::byte *>(block);
        if (std::less_equal<>{}(embedded_.data(), byte) && std::less<>{}(byte, embedded_.data() + embedded_.size())) {
            free_embedded_ |= 1U << static_cast<unsigned>((byte - embedded_.data()) / EMBEDDED_BYTES);
            return;
        }
        if (count_ == MAX_SPARES || (count_ != 0 && bytes != bytes_)) {
            ::operator delete(block);
            return;
        }
        bytes_ = bytes;
        ++count_;
        spare_ = ::new (block) Spare{spare_};
    }

  private:
    // A spare block, linked to the next one.
    struct Spare {
        Spare *next;
    };

    // Spare blocks kept at most: a burst of messages leaves no more than these behind.
    static constexpr std::size_t MAX_SPARES = 64;
    // Blocks inside the pool, for a new mailbox's list sentinels and first messages, and the bytes each holds
    // (process creation cost).
    static constexpr unsigned EMBEDDED_NODES = 4;
    static constexpr std::size_t EMBEDDED_BYTES = 4 * sizeof(void *);
    // The newest spare block, how many are kept, and their size.
    Spare *spare_ = nullptr;
    std::size_t count_ = 0;
    std::size_t bytes_ = 0;
    // One bit per embedded block not handed out.
    unsigned free_embedded_ = (1U << EMBEDDED_NODES) - 1;
    alignas(std::max_align_t) std::array<std::byte, EMBEDDED_NODES * EMBEDDED_BYTES> embedded_{};
};

// The allocator of a mailbox's lists: single nodes come from its pool (pooled message nodes). Not final: libstdc++'s
// std::list derives from its allocator.
template <typename T> class PooledAllocator {
  public:
    using value_type = T;

    explicit PooledAllocator(NodePool &pool) noexcept : pool_(&pool) {}

    template <typename U> explicit PooledAllocator(const PooledAllocator<U> &other) noexcept : pool_(other.pool()) {}

    T *allocate(std::size_t count) {
        return static_cast<T *>(count == 1 ? pool_->take(sizeof(T)) : ::operator new(count * sizeof(T)));
    }

    void deallocate(T *block, std::size_t count) noexcept {
        if (count == 1) {
            pool_->give(block, sizeof(T));
        } else {
            ::operator delete(block);
        }
    }

    NodePool *pool() const noexcept { return pool_; }

    friend bool operator==(const PooledAllocator &left, const PooledAllocator &right) noexcept {
        return left.pool_ == right.pool_;
    }

  private:
    // The pool of the mailbox whose lists allocate.
    NodePool *pool_;
};
} // namespace detail

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

    // Count the messages in the inbox and the queue; arrivals not taken yet are not counted.
    std::size_t size() const noexcept { return inbox_.size() + queue_.size(); }

    // Messages other processes sent and the owner has not taken into its heap yet (arrival buffers).
    detail::Arrivals &arrivals() noexcept { return *arrivals_; }

    // Give up the arrivals of an ended process, which lock-free sends may still reach until the executor frees them
    // (quiescent reclamation); the mailbox has none afterwards.
    std::unique_ptr<detail::Arrivals> release_arrivals() noexcept { return std::move(arrivals_); }

    // The process this one last sent to and whether it was running then: a send to a receiver that was not skips
    // the lookup of its running state (arrival buffers). Read and written by the owner only.
    struct SendHint {
        Word pid = 0;
        bool running = false;
    };

    SendHint &send_hint() noexcept { return send_hint_; }

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
    using Messages = std::list<Word, detail::PooledAllocator<Word>>;

    // Start empty; the owning context holds it.
    Mailbox() = default;

    // Nodes of both lists, kept for the next messages (pooled message nodes); outlives the lists.
    detail::NodePool nodes_;
    // Messages sent but not yet seen by a receive, oldest first.
    Messages inbox_{detail::PooledAllocator<Word>(nodes_)};
    // Messages a receive has seen, oldest first; unmatched ones stay in order.
    Messages queue_{detail::PooledAllocator<Word>(nodes_)};
    // The next message of the queue the current receive examines; end() when it has examined all.
    Messages::iterator position_ = queue_.end();
    // The current receive's timeout, set when it first waits with a finite one.
    std::optional<std::chrono::steady_clock::time_point> deadline_;
    // Messages of other processes still in their buffers (arrival buffers), allocated apart so they may outlive the
    // mailbox.
    std::unique_ptr<detail::Arrivals> arrivals_ = std::make_unique<detail::Arrivals>();
    // The last receiver of this process's sends (arrival buffers).
    SendHint send_hint_;
};
} // namespace clause::runtime
