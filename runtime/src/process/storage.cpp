#include <clause/runtime/process_context.hpp>

namespace clause::runtime {
void Mailbox::deliver(const Word message) { inbox_.push_back(message); }

std::optional<Word> Mailbox::peek() noexcept {
    if (!inbox_.empty()) {
        // Arrived messages join the queue behind those already seen; a receive past the end examines them next.
        const auto first = inbox_.begin();
        const bool at_end = position_ == queue_.end();
        queue_.splice(queue_.end(), inbox_);
        if (at_end) {
            position_ = first;
        }
    }
    return position_ == queue_.end() ? std::nullopt : std::optional{*position_};
}

void Mailbox::skip() noexcept {
    if (position_ != queue_.end()) {
        ++position_;
    }
}

std::optional<Word> Mailbox::take() noexcept {
    std::optional<Word> taken;
    if (position_ != queue_.end()) {
        taken = *position_;
        queue_.erase(position_);
    }
    restart();
    return taken;
}

void Mailbox::restart() noexcept {
    position_ = queue_.begin();
    deadline_.reset();
}
} // namespace clause::runtime
