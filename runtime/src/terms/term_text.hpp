#pragma once
#include <clause/runtime/output.hpp>

namespace clause::runtime::detail {
// Accumulate rendered bytes, latching overflow instead of growing past the caller's limit.
class TextOutput final {
  public:
    // Start empty with a fixed byte budget for the whole rendering.
    explicit TextOutput(std::size_t limit) noexcept : limit_(limit) {}

    // Append exact bytes unless that would exceed the budget; overflow discards further text.
    void append(std::string_view text);

    // Report whether any append was refused, so traversal stops with bounded work.
    bool overflowed() const noexcept { return overflowed_; }

    // Release the completed text to the caller.
    std::string take() noexcept { return std::move(text_); }

  private:
    // Rendered bytes so far; never longer than limit_.
    std::string text_;
    // Maximum total size accepted for this rendering.
    std::size_t limit_;
    // Latched once text was refused; the result is then discarded.
    bool overflowed_ = false;
};

// Scalar printers append one value in the selected style or return the term access failure.
TermResult<void> print_atom(const Term &value, TermStyle style, TextOutput &out);
TermResult<void> print_float(const Term &value, TermStyle style, TextOutput &out);
TermResult<void> print_bits(const Term &value, TermStyle style, TextOutput &out);
// Display style prints a flat list of printable Latin-1 bytes as a string; false leaves output untouched.
TermResult<bool> print_display_string(const Term &list, TextOutput &out);
} // namespace clause::runtime::detail
