#include "../string/string.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "deep.hpp"
#include <array>

// io_lib's I/O server helpers as builtins (plan 21 step LIB4C, docs/builtins.md#io-lib): collect_chars/1,3,4,
// collect_chars_eager/4, collect_line/1,3,4 and collect_line_no_eol/4, the get_chars and get_line request states of
// OTP's io_lib, clause for clause.
namespace clause::runtime::builtins::io_lib {
namespace {
using string::Terms;

// The roles of the state, data, encoding and collected stack terms (typed.hpp's Named).
struct StateRole;
struct DataRole;
struct EncodingRole;
struct StackRole;
using StateArg = Named<StateRole>;
using DataArg = Named<DataRole>;
using EncodingArg = Named<EncodingRole>;
// What a request collected so far, most recent first (OTP's Stack); the atom start before anything.
using Stack = Named<StackRole>;

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// The elements of a proper list.
std::vector<Term> elements(const Term &list) {
    std::vector<Term> out;
    Term rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        out.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return out;
}

// lists:reverse(Stack, Tail).
Term reversed(const Terms &terms, const Stack &stack, const Term &tail) {
    auto items = elements(stack.term);
    std::ranges::reverse(items);
    return terms.list(items, tail);
}

// One part of list_to_binary/1's iolist: a binary's bytes appended, a list's elements queued in order.
void append_part(const Terms &terms, const Term &part, std::vector<Term> &pending, std::vector<std::byte> &bytes) {
    if (part.is_binary()) {
        const auto b = string::binary_bytes(part);
        bytes.insert(bytes.end(), b.begin(), b.end());
        return;
    }
    auto items = elements(part);
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        const auto byte = string::small_of(*it);
        if (it->is_list() || it->is_binary()) {
            pending.push_back(*it);
        } else if (byte && *byte >= 0 && *byte <= 255) {
            pending.push_back(terms.binary(std::array{static_cast<std::byte>(*byte)}));
        } else {
            bad_argument();
        }
    }
}

// binrev/2: list_to_binary(lists:reverse(Stack, Tail)).
Term binrev(const Terms &terms, const Stack &stack, const Term &tail) {
    std::vector<std::byte> bytes;
    std::vector<Term> pending{reversed(terms, stack, tail)};
    while (!pending.empty()) {
        const auto part = pending.back();
        pending.pop_back();
        append_part(terms, part, pending, bytes);
    }
    return terms.binary(bytes);
}

// count_and_find_utf8/2: the UTF-8 characters before the first invalid one, and the byte offset after the first N.
std::pair<std::int64_t, std::optional<std::size_t>> count_utf8(const Term &data, std::int64_t n) {
    if (!data.is_binary()) {
        return {0, std::nullopt};
    }
    const auto bytes = string::binary_bytes(data);
    std::int64_t count = 0;
    std::optional<std::size_t> position;
    for (std::size_t at = 0; at < bytes.size();) {
        const auto start = at;
        if (!next_utf8(bytes, at)) {
            break;
        }
        if (count == n) {
            position = start;
        }
        ++count;
    }
    return {count, position};
}

// A get_chars step over a binary: the `size` units it holds of the `wanted`, and the byte to split it at.
struct Split {
    std::int64_t size = 0;
    std::size_t at = 0;
    std::int64_t wanted = 0;
};

class Collector final {
  public:
    explicit Collector(ProcessContext &context) : terms_(context) {}

    Term tuple(std::initializer_list<Term> items) const { return terms_.tuple(std::span(items.begin(), items.size())); }

    Term atom(const AtomName name) const { return terms_.atom(name); }

    const Terms &terms() const { return terms_; }

    // {stop, eof, eof}.
    Term at_eof() const { return tuple({atom("stop"_atom), atom("eof"_atom), atom("eof"_atom)}); }

    // collect_chars/4 in state start.
    Term start(const DataArg &data, const EncodingArg &encoding, std::optional<std::int64_t> n) const {
        const auto unicode = unicode_of(encoding.term);
        if (data.term.is_binary() && n && unicode) {
            return chars_binary({atom("start"_atom)}, data, *unicode, *n);
        }
        if (data.term.is_list() && n) {
            return chars_list({terms_.nil()}, *n, data);
        }
        if (is_atom(data.term, "eof")) {
            return at_eof();
        }
        no_clause();
    }

    // collect_chars/4 in a {Left, Sofar}, {binary, Stack, N} or {list, Stack, N} state.
    Term tagged(const std::vector<Term> &items, const DataArg &data, const EncodingArg &encoding) const {
        if (items.size() == 2 && string::small_of(items[0])) {
            return chars1(*string::small_of(items[0]), data, {items[1]});
        }
        const bool three = items.size() == 3;
        const auto left = three ? string::small_of(items[2]) : std::nullopt;
        if (three && is_atom(items[0], "binary")) {
            return binary_state({items[1]}, data, encoding, left);
        }
        if (three && is_atom(items[0], "list") && left) {
            return chars_list({items[1]}, *left, data);
        }
        no_clause();
    }

    // collect_chars1/3.
    Term chars1(std::int64_t n, const DataArg &chars, Stack stack) const {
        Term rest = chars.term;
        for (; n > 0 && rest.is_cons(); --n, rest = rest.tail_unchecked()) {
            stack.term = terms_.cons(rest.head_unchecked(), stack.term);
        }
        if (n <= 0) {
            return tuple({atom("done"_atom), reversed(terms_, stack, terms_.nil()), rest});
        }
        if (is_atom(rest, "eof")) {
            const auto taken = stack.term.is_nil() ? atom("eof"_atom) : reversed(terms_, stack, terms_.nil());
            return tuple({atom("done"_atom), taken, terms_.nil()});
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        return tuple({atom("more"_atom), tuple({terms_.integer(n), stack.term})});
    }

    // collect_line_bin/4.
    Term line_binary(const DataArg &data, const Stack &stack) const {
        const auto bytes = string::binary_bytes(data.term);
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            const auto byte = std::to_integer<char>(bytes[i]);
            if (byte == '\n') {
                return line_end(data, stack, i + 1);
            }
            if (byte == '\r' && i + 1 < bytes.size() && std::to_integer<char>(bytes[i + 1]) == '\n') {
                const auto line = terms_.slice(data.term, 0, i);
                const auto rest = terms_.slice(data.term, i + 2, bytes.size() - i - 2);
                const auto ending = terms_.list(std::array{line, terms_.integer('\n')});
                return tuple({atom("stop"_atom), binrev(terms_, stack, ending), rest});
            }
            if (byte == '\r' && i + 1 == bytes.size()) {
                const auto cr = terms_.binary(std::array{std::byte{'\r'}});
                return terms_.cons(cr, terms_.cons(terms_.slice(data.term, 0, i), stack.term));
            }
        }
        return terms_.cons(data.term, stack.term);
    }

    // collect_line_list/2.
    Term line_list(const DataArg &data, Stack stack) const {
        Term rest = data.term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto head = rest.head_unchecked();
            if (string::is_char(head, '\n')) {
                if (stack.term.is_cons() && string::is_char(stack.term.head_unchecked(), '\r')) {
                    stack.term = stack.term.tail_unchecked();
                }
                const auto line = reversed(terms_, stack, terms_.list(std::array{head}));
                return tuple({atom("stop"_atom), line, rest.tail_unchecked()});
            }
            stack.term = terms_.cons(head, stack.term);
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        return stack.term;
    }

  private:
    // Whether an encoding is unicode or latin1; none for anything else.
    static std::optional<bool> unicode_of(const Term &encoding) {
        if (is_atom(encoding, "unicode") || is_atom(encoding, "latin1")) {
            return is_atom(encoding, "unicode");
        }
        return std::nullopt;
    }

    // collect_chars/4 in a {binary, Stack, N} state.
    Term binary_state(const Stack &stack, const DataArg &data, const EncodingArg &encoding,
                      std::optional<std::int64_t> left) const {
        if (is_atom(data.term, "eof")) {
            const auto items = elements(stack.term);
            const bool empty = items.size() == 1 && items[0].is_binary() && string::binary_bytes(items[0]).empty();
            return empty ? at_eof() : tuple({atom("stop"_atom), binrev(terms_, stack, terms_.nil()), atom("eof"_atom)});
        }
        const auto unicode = unicode_of(encoding.term);
        if (!left || !unicode) {
            no_clause();
        }
        return chars_binary(stack, data, *unicode, *left);
    }

    // Split `data` as `split` says: {stop, Taken, Rest} with enough, else {binary, Stack, Left}.
    Term binary_step(const Stack &stack, const DataArg &data, const Split &split) const {
        const auto bytes = string::binary_bytes(data.term).size();
        if (split.size > split.wanted) {
            const auto first = terms_.slice(data.term, 0, split.at);
            const auto rest = terms_.slice(data.term, split.at, bytes - split.at);
            return tuple({atom("stop"_atom), with_stack(stack, first), rest});
        }
        if (split.size < split.wanted) {
            const auto pushed = terms_.cons(data.term, stack.term.is_atom() ? terms_.nil() : stack.term);
            return tuple({atom("binary"_atom), pushed, terms_.integer(split.wanted - split.size)});
        }
        return tuple({atom("stop"_atom), with_stack(stack, data.term), terms_.binary({})});
    }

    // The collected binary: Data alone at the start, else binrev(Stack, [Data]).
    Term with_stack(const Stack &stack, const Term &data) const {
        return stack.term.is_atom() ? data : binrev(terms_, stack, terms_.list(std::array{data}));
    }

    // One get_chars step over a binary in `encoding` (`stack` is the atom start at first).
    Term chars_binary(const Stack &stack, const DataArg &data, bool unicode, std::int64_t n) const {
        if (!unicode) {
            const auto size =
                data.term.is_binary() ? static_cast<std::int64_t>(string::binary_bytes(data.term).size()) : -1;
            if (size < 0 || (size > n && n < 0)) {
                bad_argument();
            }
            return binary_step(
                stack, data, {.size = size, .at = static_cast<std::size_t>(std::max<std::int64_t>(n, 0)), .wanted = n});
        }
        const auto [size, position] = count_utf8(data.term, n);
        if (size > n && !position) {
            bad_argument();
        }
        return binary_step(stack, data, {.size = size, .at = position.value_or(0), .wanted = n});
    }

    // collect_chars_list/3.
    Term chars_list(Stack stack, std::int64_t n, const DataArg &data) const {
        if (n == 0) {
            return tuple({atom("stop"_atom), reversed(terms_, stack, terms_.nil()), data.term});
        }
        if (is_atom(data.term, "eof")) {
            return stack.term.is_nil()
                       ? at_eof()
                       : tuple({atom("stop"_atom), reversed(terms_, stack, terms_.nil()), atom("eof"_atom)});
        }
        Term rest = data.term;
        for (; rest.is_cons() && n != 0; rest = rest.tail_unchecked(), --n) {
            stack.term = terms_.cons(rest.head_unchecked(), stack.term);
        }
        if (n == 0) {
            return tuple({atom("stop"_atom), reversed(terms_, stack, terms_.nil()), rest});
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        return tuple({atom("list"_atom), stack.term, terms_.integer(n)});
    }

    // collect_line_bin/4 at a newline ending at byte `n`.
    Term line_end(const DataArg &data, const Stack &stack, std::size_t n) const {
        const auto size = string::binary_bytes(data.term).size();
        const auto line = terms_.slice(data.term, 0, n);
        const auto rest = terms_.slice(data.term, n, size - n);
        if (stack.term.is_nil()) {
            return tuple({atom("stop"_atom), line, rest});
        }
        const auto top = need(stack.term.head());
        if (n == 1 && top.is_binary() && string::binary_bytes(top).size() == 1 &&
            std::to_integer<char>(string::binary_bytes(top)[0]) == '\r') {
            const auto newline = terms_.list(std::array{terms_.integer('\n')});
            return tuple({atom("stop"_atom), binrev(terms_, {need(stack.term.tail())}, newline), rest});
        }
        return tuple({atom("stop"_atom), binrev(terms_, stack, terms_.list(std::array{line})), rest});
    }

    Terms terms_;
};

// collect_chars/4.
Term collect_chars4(ProcessContext &context, const StateArg &state, const DataArg &data, const EncodingArg &encoding,
                    const Term &count) {
    const Collector collect(context);
    const auto n = string::small_of(count);
    const auto &tag = state.term;
    if (is_atom(tag, "start")) {
        return collect.start(data, encoding, n);
    }
    if (tag.is_nil() && n) {
        return collect.chars1(*n, data, {collect.terms().nil()});
    }
    if (!tag.is_tuple()) {
        no_clause();
    }
    return collect.tagged(need(tag.tuple_elements()), data, encoding);
}

Term collect_chars3(ProcessContext &context, const StateArg &state, const DataArg &data, const Term &count) {
    return collect_chars4(context, state, data, {Terms(context).atom("latin1"_atom)}, count);
}

// collect_chars/1: the N of a 3-tuple state.
Term collect_chars1(ProcessContext & /*context*/, const Term &state) {
    if (!state.is_tuple() || state.tuple_size_unchecked() != 3) {
        no_clause();
    }
    return state.tuple_element_unchecked(2);
}

Term collect_chars_eager4(ProcessContext &context, const StateArg &state, const DataArg &data,
                          const EncodingArg &encoding, const Term &count) {
    auto result = collect_chars4(context, state, data, encoding, count);
    const Terms terms(context);
    if (!result.is_tuple() || result.tuple_size_unchecked() != 3) {
        return result;
    }
    const auto tag = result.tuple_element_unchecked(0);
    const Stack stack{result.tuple_element_unchecked(1)};
    if (is_atom(tag, "list") && !stack.term.is_nil()) {
        return terms.tuple(std::array{terms.atom("stop"_atom), reversed(terms, stack, terms.nil()), terms.nil()});
    }
    const auto &items = stack.term;
    const bool empty_binary = items.is_cons() && items.tail_unchecked().is_nil() &&
                              items.head_unchecked().is_binary() &&
                              string::binary_bytes(items.head_unchecked()).empty();
    if (is_atom(tag, "binary") && !empty_binary) {
        return terms.tuple(std::array{terms.atom("stop"_atom), binrev(terms, stack, terms.nil()), terms.nil()});
    }
    return result;
}

// collect_line/4.
Term collect_line4(ProcessContext &context, const StateArg &state, const DataArg &data,
                   const EncodingArg & /*encoding*/, const Term & /*any*/) {
    const Collector collect(context);
    const auto &terms = collect.terms();
    const bool start = is_atom(state.term, "start");
    const Stack stack{start ? terms.nil() : state.term};
    if (data.term.is_binary()) {
        return collect.line_binary(data, stack);
    }
    if (data.term.is_list()) {
        return collect.line_list(data, stack);
    }
    if (!is_atom(data.term, "eof")) {
        no_clause();
    }
    if (start) {
        return collect.at_eof();
    }
    if (stack.term.is_cons() && stack.term.head_unchecked().is_binary()) {
        return collect.tuple({collect.atom("stop"_atom), binrev(terms, stack, terms.nil()), collect.atom("eof"_atom)});
    }
    return collect.tuple({collect.atom("stop"_atom), reversed(terms, stack, terms.nil()), collect.atom("eof"_atom)});
}

Term collect_line3(ProcessContext &context, const StateArg &state, const DataArg &data, const Term &any) {
    return collect_line4(context, state, data, {Terms(context).atom("latin1"_atom)}, any);
}

Term collect_line1(ProcessContext &context, const Term & /*state*/) { return Terms(context).integer(0); }

// collect_line_no_eol/4: a collected line trimmed by string:trim/1.
Term collect_line_no_eol4(ProcessContext &context, const StateArg &state, const DataArg &data,
                          const EncodingArg &encoding, const Term &any) {
    auto result = collect_line4(context, state, data, encoding, any);
    if (!result.is_tuple() || result.tuple_size_unchecked() != 3 ||
        !is_atom(result.tuple_element_unchecked(0), "stop") || is_atom(result.tuple_element_unchecked(1), "eof")) {
        return result;
    }
    const Terms terms(context);
    const auto line = string::trim_of(context, result.tuple_element_unchecked(1));
    return terms.tuple(std::array{result.tuple_element_unchecked(0), line, result.tuple_element_unchecked(2)});
}

constexpr std::array IO_LIB_COLLECT_BUILTINS{
    typed_entry<collect_chars1>("io_lib", "collect_chars"),
    typed_entry<collect_chars3>("io_lib", "collect_chars"),
    typed_entry<collect_chars4>("io_lib", "collect_chars"),
    typed_entry<collect_chars_eager4>("io_lib", "collect_chars_eager"),
    typed_entry<collect_line1>("io_lib", "collect_line"),
    typed_entry<collect_line3>("io_lib", "collect_line"),
    typed_entry<collect_line4>("io_lib", "collect_line"),
    typed_entry<collect_line_no_eol4>("io_lib", "collect_line_no_eol"),
};
} // namespace
} // namespace clause::runtime::builtins::io_lib

namespace clause::runtime {
std::span<const BuiltinEntry> io_lib_collect_builtins() noexcept { return builtins::io_lib::IO_LIB_COLLECT_BUILTINS; }
} // namespace clause::runtime
