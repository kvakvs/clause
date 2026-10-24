#include "term_text.hpp"
#include "funs.hpp"
#include "identities.hpp"
#include "records.hpp"
#include <clause/abi/equality.hpp>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace clause::runtime {
namespace detail {
void TextOutput::append(std::string_view text) {
    if (overflowed_ || text.size() > limit_ - text_.size()) {
        overflowed_ = true;
        return;
    }
    text_.append(text);
}
} // namespace detail

namespace {
enum class FrameKind : std::uint8_t { tuple, list, map, record };

struct Frame {
    // Tuple under traversal, or the unprinted remainder of a list spine.
    Term rest;
    // Map associations or record fields in print order; empty for tuples and lists.
    std::vector<std::pair<Term, Term>> entries;
    // Next tuple field, record field or map position (keys even, values odd); lists count printed elements.
    std::size_t next = 0;
    // Tuple arity, record field count or twice the map size.
    std::size_t count = 0;
    // Selects separators and how the next child is found.
    FrameKind kind = FrameKind::tuple;
};

using Child = TermResult<std::optional<Term>>;

// Print a term graph with an explicit frame stack; each step appends text, so work tracks the output limit.
class Renderer final {
  public:
    // Bind the style and byte budget for one rendering.
    Renderer(TermStyle style, std::size_t limit) : style_(style), out_(limit) {}

    // Render the whole term, or fail with the first access error or resource_limit on overflow.
    TermResult<std::string> run(const Term &root) {
        out_.reserve(expected_size(root));
        auto status = value(root);
        while (status && !frames_.empty() && !out_.overflowed()) {
            auto child = advance(frames_.back());
            if (!child) {
                return std::unexpected(child.error());
            }
            if (*child) {
                status = value(**child);
            } else {
                frames_.pop_back();
            }
        }
        if (!status) {
            return std::unexpected(status.error());
        }
        return out_.overflowed() ? TermResult<std::string>{std::unexpected(TermError::resource_limit)} : out_.take();
    }

  private:
    // Containers usually print past the small-string buffer; scalars stay unreserved.
    static std::size_t expected_size(const Term &root) {
        return root.is_cons() || root.is_tuple() || root.is_map() ? 256 : 0;
    }

    // Print a scalar directly or open a container frame.
    TermResult<void> value(const Term &term) {
        if (term.is_integer()) {
            const auto text = term.integer_decimal();
            if (!text) {
                return std::unexpected(text.error());
            }
            out_.append(*text);
            return {};
        }
        if (term.is_float()) {
            return detail::print_float(term, style_, out_);
        }
        if (term.is_atom()) {
            return detail::print_atom(term, style_, out_);
        }
        if (term.is_function()) {
            return detail::print_fun(term, style_, out_);
        }
        if (term.is_pid() || term.is_port() || term.is_reference()) {
            return detail::print_identity(term, out_);
        }
        return term.is_bitstring() ? detail::print_bits(term, style_, out_) : open(term);
    }

    // Containers write their opening text; lists may print whole as display strings instead.
    TermResult<void> open(const Term &term) {
        if (term.is_nil()) {
            out_.append("[]");
            return {};
        }
        if (term.is_cons()) {
            return open_list(term);
        }
        if (term.is_tuple()) {
            const auto size = term.tuple_size_unchecked();
            out_.append("{");
            frames_.push_back(Frame{term, {}, 0, size, FrameKind::tuple});
            return {};
        }
        if (term.is_native_record()) {
            return open_record(term);
        }
        return term.is_map() ? open_map(term) : TermResult<void>{std::unexpected(TermError::not_implemented)};
    }

    // Native records print as #Module:Name{ with their fields in definition order.
    TermResult<void> open_record(const Term &term) {
        const auto identity = detail::record_identity(term);
        auto fields = term.record_fields();
        if (!identity || !fields) {
            return std::unexpected(identity ? fields.error() : identity.error());
        }
        out_.append("#");
        const auto module = detail::print_atom(identity->first, style_, out_);
        out_.append(":");
        const auto name = detail::print_atom(identity->second, style_, out_);
        if (!module || !name) {
            return std::unexpected(module ? name.error() : module.error());
        }
        out_.append("{");
        const auto count = fields->size();
        frames_.push_back(Frame{Term{}, std::move(*fields), 0, count, FrameKind::record});
        return {};
    }

    // Display style prints printable byte lists as strings; otherwise elements follow '['.
    TermResult<void> open_list(const Term &term) {
        if (style_ == TermStyle::display) {
            const auto string = detail::print_display_string(term, out_);
            if (!string || *string) {
                return string ? TermResult<void>{} : std::unexpected(string.error());
            }
        }
        out_.append("[");
        frames_.push_back(Frame{term, {}, 0, 0, FrameKind::list});
        return {};
    }

    // Maps print their associations in canonical key order.
    TermResult<void> open_map(const Term &term) {
        auto entries = term.map_entries();
        if (!entries) {
            return std::unexpected(entries.error());
        }
        out_.append("#{");
        const auto count = entries->size() * 2;
        frames_.push_back(Frame{Term{}, std::move(*entries), 0, count, FrameKind::map});
        return {};
    }

    // Emit the separator before the next child, or the closing text when the frame is done.
    Child advance(Frame &frame) {
        if (frame.kind == FrameKind::list) {
            return advance_list(frame);
        }
        if (frame.next == frame.count) {
            out_.append("}");
            return std::nullopt;
        }
        if (frame.kind == FrameKind::record) {
            return advance_record(frame);
        }
        return frame.kind == FrameKind::tuple ? advance_tuple(frame) : advance_map(frame);
    }

    // Each field prints its name, then its value; io_lib pads the '=' with spaces, the emulator does not.
    Child advance_record(Frame &frame) {
        const auto &field = frame.entries[frame.next];
        out_.append(frame.next++ == 0 ? "" : ",");
        if (const auto name = detail::print_atom(field.first, style_, out_); !name) {
            return std::unexpected(name.error());
        }
        out_.append(style_ != TermStyle::display ? " = " : "=");
        return field.second;
    }

    // Tuple fields are comma separated.
    Child advance_tuple(Frame &frame) {
        out_.append(frame.next == 0 ? "" : ",");
        return frame.rest.tuple_element_unchecked(frame.next++);
    }

    // Keys and values alternate; io_lib pads the arrow with spaces, the emulator does not.
    Child advance_map(Frame &frame) {
        const auto &entry = frame.entries[frame.next / 2];
        const bool key = frame.next % 2 == 0;
        if (key) {
            out_.append(frame.next == 0 ? "" : ",");
        } else {
            out_.append(style_ != TermStyle::display ? " => " : "=>");
        }
        ++frame.next;
        return key ? entry.first : entry.second;
    }

    // Elements are comma separated; an improper tail follows '|' and is printed once.
    Child advance_list(Frame &frame) {
        if (frame.rest.is_nil()) {
            out_.append("]");
            return std::nullopt;
        }
        if (!frame.rest.is_cons()) {
            out_.append("|");
            return std::exchange(frame.rest, Term::from_word(abi::v1::empty_list).value());
        }
        out_.append(frame.next++ == 0 ? "" : ",");
        auto head = frame.rest.head_unchecked();
        auto tail = frame.rest.tail_unchecked();
        frame.rest = tail;
        return head;
    }

    // Selects io_lib or emulator rules for every printed value.
    TermStyle style_;
    // Bounded text accumulated so far.
    detail::TextOutput out_;
    // Open containers, innermost last; depth is limited only by the term itself.
    std::vector<Frame> frames_;
};
} // namespace

TermResult<std::string> format_term(const Term &value, TermStyle style, std::size_t limit) noexcept {
    try {
        return Renderer(style, limit).run(value);
    } catch (const std::bad_alloc &) {
        return std::unexpected(TermError::out_of_memory);
    } catch (const std::length_error &) {
        return std::unexpected(TermError::resource_limit);
    } catch (...) {
        return std::unexpected(TermError::invalid_argument);
    }
}
} // namespace clause::runtime
