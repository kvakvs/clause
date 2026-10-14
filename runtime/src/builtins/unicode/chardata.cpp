#include "chardata.hpp"
#include <algorithm>
#include <terms.hpp>

namespace clause::runtime::builtins::unicode {
namespace {
using Outcome = Conversion::Outcome;

// The walk of one chardata term: a stack of enclosing list tails, bytes of an unfinished multi-byte sequence carried
// from one binary to the next, and the result.
class Walk final {
  public:
    Walk(ProcessContext &context, Encoding input, bool latin1_output)
        : factory_(context), input_(input), latin1_output_(latin1_output) {}

    // Walk `data` to its end or to the first character that does not convert.
    Conversion run(const Term &data) {
        auto current = data;
        while (result_.outcome == Outcome::done && !finished(current)) {
            current = step(current);
        }
        if (result_.outcome == Outcome::done && !carry_.empty()) {
            result_.outcome = Outcome::incomplete;
            result_.rest = carried();
        }
        return std::move(result_);
    }

  private:
    // Whether the walk is at the end: `current` is [] with no enclosing list left; pops finished levels.
    bool finished(Term &current) {
        while (current.is_nil()) {
            if (tails_.empty()) {
                return true;
            }
            current = tails_.back();
            tails_.pop_back();
        }
        return false;
    }

    // Convert the head of `current` (a cons or a binary); returns what remains at this level.
    Term step(const Term &current) {
        if (current.is_binary()) {
            binary(current);
            return need(factory_.nil());
        }
        if (!current.is_cons()) {
            bad_argument();
        }
        auto head = need(current.head());
        auto tail = need(current.tail());
        if (head.is_integer()) {
            integer(current);
        } else if (head.is_binary()) {
            binary(head, tail);
        } else if (head.is_list()) {
            tails_.push_back(tail);
            return head;
        } else {
            bad_argument();
        }
        return tail;
    }

    // The integer element at the start of `list`.
    void integer(const Term &list) {
        const auto head = need(list.head());
        const auto code = small(head.word());
        if (!carry_.empty()) {
            // OTP puts the unfinished bytes in front of the whole rest.
            result_.outcome = Outcome::error;
            result_.rest = need(factory_.list(std::array{carried(), wrapped(list)}));
        } else if (!code || !scalar(*code) || (input_.kind == Encoding::Kind::latin1 && *code > 255)) {
            fail(list);
        } else if (latin1_output_ && *code > 255) {
            fail(need(factory_.cons(need(factory_.list(std::array{head})), need(list.tail()))));
        } else {
            result_.codes.push_back(static_cast<char32_t>(*code));
        }
    }

    // A binary element followed by `tail`, or without one the binary at the top of a level.
    void binary(const Term &value, const std::optional<Term> &tail = std::nullopt) {
        auto bytes = carry_;
        carry_.clear();
        const auto own = need(value.binary_bytes());
        bytes.insert(bytes.end(), own.begin(), own.end());
        const auto before = result_.codes.size();
        const auto decoded = decode(bytes, input_, result_.codes);
        const auto stop = latin1_stop(before, bytes);
        if (decoded.invalid || stop) {
            const auto at = stop.value_or(decoded.consumed);
            const auto suffix = need(factory_.binary(std::span(bytes).subspan(at)));
            // A binary ending the top level is its rest alone; anywhere else it heads the rest of its list.
            const bool alone = !tail || (tails_.empty() && tail->is_nil());
            fail(alone ? suffix : need(factory_.cons(suffix, *tail)));
            return;
        }
        carry_.assign(bytes.begin() + static_cast<std::ptrdiff_t>(decoded.consumed), bytes.end());
    }

    // With latin1 output, the byte offset of the first decoded code above 255, dropping it and what follows.
    std::optional<std::size_t> latin1_stop(std::size_t before, const Bytes &bytes) {
        if (!latin1_output_) {
            return std::nullopt;
        }
        const auto first = result_.codes.begin() + static_cast<std::ptrdiff_t>(before);
        const auto wide = std::find_if(first, result_.codes.end(), [](char32_t code) { return code > 255; });
        if (wide == result_.codes.end()) {
            return std::nullopt;
        }
        const auto kept = static_cast<std::size_t>(wide - first);
        result_.codes.resize(before + kept);
        return encoded_length(kept, bytes);
    }

    // The fewest bytes at the start of `bytes` that decode to `count` code points (a binary search: the count only
    // grows with the length).
    std::size_t encoded_length(std::size_t count, const Bytes &bytes) const {
        std::size_t low = 0;
        std::size_t high = bytes.size();
        while (low < high) {
            const auto middle = low + (high - low) / 2;
            std::u32string decoded;
            static_cast<void>(decode(std::span(bytes).first(middle), input_, decoded));
            if (decoded.size() >= count) {
                high = middle;
            } else {
                low = middle + 1;
            }
        }
        return low;
    }

    // The carried bytes as a binary.
    Term carried() { return need(factory_.binary(carry_)); }

    // `innermost` inside every enclosing list's remaining tail, innermost first.
    Term wrapped(Term innermost) {
        for (auto tail = tails_.rbegin(); tail != tails_.rend(); ++tail) {
            innermost = need(factory_.cons(innermost, *tail));
        }
        return innermost;
    }

    // Stop with an error at `innermost`, the unconverted rest of the innermost level.
    void fail(const Term &innermost) {
        result_.outcome = Outcome::error;
        result_.rest = wrapped(innermost);
    }

    TermFactory factory_;
    // The encoding of binaries in the input, and whether the output takes only codes up to 255.
    Encoding input_;
    bool latin1_output_;
    // The tails of the enclosing lists, outermost first.
    std::vector<Term> tails_;
    // Bytes of a multi-byte sequence a binary ended in, decoded with the next binary.
    Bytes carry_;
    Conversion result_;
};
} // namespace

Conversion convert(ProcessContext &context, const Term &data, Encoding input, bool latin1_output) {
    if (!data.is_list() && !data.is_binary()) {
        bad_argument();
    }
    return Walk(context, input, latin1_output).run(data);
}
} // namespace clause::runtime::builtins::unicode
