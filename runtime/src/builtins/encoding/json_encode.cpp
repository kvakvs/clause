#include "../../terms/integers.hpp"
#include "../float_text.hpp"
#include "../lists/list_walk.hpp"
#include "json.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The default paths of json's encoder and formatter (plan 21 step LIB5F, docs/builtins.md#encodings): encode/1 and
// format/1,2 without user funs, building the same iodata as json.erl's encode_value/2 and format_value/3 (which in
// turn build OTP's). Nesting deeper than DEPTH, or a format state json.erl's arithmetic would reject, answers
// `fallback`, and json.erl encodes the term itself.
namespace clause::runtime::builtins::encoding {
namespace {
using lists::no_clause;

// The nesting the C++ walks take on the scheduler's stack before handing the term to Erlang.
constexpr int DEPTH = 256;

// Thrown to hand the whole term to json.erl.
struct Fallback {};

struct TermRole;
struct StateRole;
// A term to encode or format.
using Value = Named<TermRole>;
// format's state map.
using FormatState = Named<StateRole>;

// Builds iodata terms.
class Pieces {
  public:
    explicit Pieces(ProcessContext &context) : context_(context) {}

    Term integer(std::int64_t value) const { return need(TermFactory(context_).integer(value)); }

    Term cons(const Term &head, const Term &tail) const { return need(TermFactory(context_).cons(head, tail)); }

    Term list(std::span<const Term> items) const { return need(TermFactory(context_).list(items)); }

    Piece binary(std::string_view text) const {
        return {.term = need(TermFactory(context_).binary(std::as_bytes(std::span(text)))), .size = text.size()};
    }

    // A string literal of json.erl: a list of its characters.
    Piece chars(std::string_view text) const {
        std::vector<Term> codes;
        for (const auto c : text) {
            codes.push_back(integer(static_cast<unsigned char>(c)));
        }
        return {.term = list(codes), .size = text.size()};
    }

    ProcessContext &context() const { return context_; }

  private:
    ProcessContext &context_;
};

[[noreturn]] void unsupported(ProcessContext &context, const Term &value) {
    TermFactory factory(context);
    const std::array reason{need(factory.atom("unsupported_type")), value};
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error, .payload = need(factory.tuple(reason)).word()};
}

std::string integer_text(const Term &value) { return detail::integer_text(need(detail::integer_read(value))); }

std::string float_short(const Term &value) {
    return *float_text(value.float_value_unchecked(), FloatFormat{.kind = FloatFormat::Kind::shortest});
}

// A scalar's encoding: the literals, a string for other atoms and binaries, a number's text; none for a container.
std::optional<Piece> scalar(const Pieces &pieces, const Term &value) {
    if (value.is_atom()) {
        const auto name = value.atom_spelling_unchecked();
        if (name == "null" || name == "true" || name == "false") {
            return pieces.binary(name);
        }
        return escaped(pieces.context(), pieces.binary(name).term, false);
    }
    if (value.is_binary()) {
        return escaped(pieces.context(), value, false);
    }
    if (value.is_integer()) {
        return pieces.binary(integer_text(value));
    }
    if (value.is_float()) {
        return pieces.binary(float_short(value));
    }
    if (value.is_list() || value.is_map()) {
        return std::nullopt;
    }
    unsupported(pieces.context(), value);
}

// An object key: strings for binaries and atoms, quoted numbers; function_clause for anything else.
Piece key(const Pieces &pieces, const Term &key) {
    if (key.is_binary()) {
        return escaped(pieces.context(), key, false);
    }
    if (key.is_atom()) {
        return escaped(pieces.context(), pieces.binary(key.atom_spelling_unchecked()).term, false);
    }
    if (!key.is_integer() && !key.is_float()) {
        no_clause();
    }
    const auto text = pieces.binary(key.is_integer() ? integer_text(key) : float_short(key));
    const auto quote = pieces.integer('"');
    const std::array items{quote, text.term, quote};
    return {.term = pieces.list(items), .size = text.size + 2};
}

// A list's elements in order; function_clause at an improper tail, as json.erl reaches it.
template <typename Visit> void each_element(Term list, Visit visit) {
    for (; list.is_cons(); list = list.tail_unchecked()) {
        visit(list.head_unchecked());
    }
    if (!list.is_nil()) {
        no_clause();
    }
}

// encode/1: json.erl's encode_value/2 with itself as the encoder.
class Encoder final {
  public:
    explicit Encoder(ProcessContext &context) : pieces_(context) {}

    Term value(const Term &term, int depth) {
        if (const auto piece = scalar(pieces_, term)) {
            return piece->term;
        }
        if (depth == DEPTH) {
            throw Fallback{};
        }
        return term.is_map() ? object(term, depth + 1) : array(term, depth + 1);
    }

  private:
    // [$[, First, $,, Second, ..., $]] or <<"[]">>.
    Term array(const Term &list, int depth) {
        if (list.is_nil()) {
            return pieces_.binary("[]").term;
        }
        std::vector<Term> items;
        each_element(list, [&](const Term &element) {
            items.push_back(pieces_.integer(items.empty() ? '[' : ','));
            items.push_back(value(element, depth));
        });
        items.push_back(pieces_.integer(']'));
        return pieces_.list(items);
    }

    // [${, [Key, $: | Value], [[$,, Key, $: | Value], ...], $}] or <<"{}">>.
    Term object(const Term &map, int depth) {
        const auto entries = need(map.map_entries());
        if (entries.empty()) {
            return pieces_.binary("{}").term;
        }
        std::vector<Term> members;
        for (const auto &[name, member] : entries) {
            const auto encoded = key(pieces_, name).term;
            members.push_back(pieces_.cons(encoded, pieces_.cons(pieces_.integer(':'), value(member, depth))));
        }
        std::vector<Term> rest;
        for (std::size_t index = 1; index < members.size(); ++index) {
            rest.push_back(pieces_.cons(pieces_.integer(','), members[index]));
        }
        const std::array items{pieces_.integer('{'), members[0], pieces_.list(rest), pieces_.integer('}')};
        return pieces_.list(items);
    }

    Pieces pieces_;
};

// format's state: the nesting level, the current column, spaces per level and the line width.
struct Layout {
    std::int64_t level;
    std::int64_t column;
    std::int64_t indent;
    std::int64_t max;
};

// A state field: a small integer, else json.erl takes the term (its arithmetic decides what happens).
std::int64_t field(ProcessContext &context, const Term &state, std::string_view name) {
    const auto value = need(state.map_find(need(TermFactory(context).atom(name))));
    if (!value || !value->is_integer()) {
        throw Fallback{};
    }
    const auto number = value->integer_value();
    if (!number || *number < 0 || *number > (std::int64_t{1} << 40)) {
        throw Fallback{};
    }
    return *number;
}

// format/1,2: json.erl's format_value/3 with itself as the formatter.
class Formatter final {
  public:
    explicit Formatter(ProcessContext &context) : pieces_(context) {}

    Piece value(const Term &term, const Layout &layout, int depth) {
        if (const auto piece = scalar(pieces_, term)) {
            return *piece;
        }
        if (depth == DEPTH) {
            throw Fallback{};
        }
        return term.is_map() ? object(term, layout, depth + 1) : array(term, layout, depth + 1);
    }

  private:
    // The column and the line break of a level: a binary up to 20 even spaces, else ["\n", [" ", ...]].
    Piece line_break(const Layout &layout) const {
        const auto width = layout.level * layout.indent;
        if (width <= 20 && width % 2 == 0) {
            return pieces_.binary("\n" + std::string(static_cast<std::size_t>(width), ' '));
        }
        std::vector<Term> spaces(static_cast<std::size_t>(width), pieces_.chars(" ").term);
        const std::array items{pieces_.chars("\n").term, pieces_.list(spaces)};
        return {.term = pieces_.list(items), .size = static_cast<std::size_t>(width) + 1};
    }

    static Layout nested(const Layout &layout) { return {layout.level + 1, layout.column, layout.indent, layout.max}; }

    static Layout at(const Layout &layout, std::int64_t column) {
        return {layout.level, column, layout.indent, layout.max};
    }

    Piece sum(std::span<const Piece> parts) const {
        std::vector<Term> items;
        std::size_t size = 0;
        for (const auto &part : parts) {
            items.push_back(part.term);
            size += part.size;
        }
        return {.term = pieces_.list(items), .size = size};
    }

    Piece byte(char c) const { return {.term = pieces_.integer(static_cast<unsigned char>(c)), .size = 1}; }

    Piece array(const Term &list, const Layout &layout, int depth) {
        if (list.is_nil()) {
            return pieces_.binary("[]");
        }
        const auto first = list.head_unchecked();
        const auto inner = nested(layout);
        const auto indent = line_break(inner);
        if (first.is_list() || first.is_map() || first.is_binary() || layout.column > layout.max) {
            return lined(list, at(inner, inner.level * inner.indent), indent, line_break(layout), depth);
        }
        return running(list, inner, indent, depth);
    }

    // One element per line: [$[, Indent, First, [[[$, | Indent] | Element], ...], Closing, $]].
    Piece lined(const Term &list, const Layout &layout, const Piece &indent, const Piece &closing, int depth) {
        std::optional<Piece> head;
        std::vector<Piece> rest;
        each_element(list, [&](const Term &element) {
            auto piece = value(element, layout, depth);
            if (!head) {
                head = piece;
                return;
            }
            rest.push_back({.term = pieces_.cons(pieces_.cons(pieces_.integer(','), indent.term), piece.term),
                            .size = 1 + indent.size + piece.size});
        });
        const std::array parts{byte('['), indent, *head, sum(rest), closing, byte(']')};
        return sum(parts);
    }

    // Elements along the line, wrapping once the column reaches `max`: [$[, First, [[$, | Element], ...], $]].
    Piece running(const Term &list, const Layout &layout, const Piece &indent, int depth) {
        std::optional<Piece> head;
        std::vector<Piece> rest;
        auto column = layout.column;
        each_element(list, [&](const Term &element) {
            const bool along = column < layout.max;
            auto piece = value(element, at(layout, column), depth);
            if (!head) {
                head = piece;
                column += 1 + static_cast<std::int64_t>(piece.size);
            } else if (along) {
                rest.push_back({.term = pieces_.cons(pieces_.integer(','), piece.term), .size = 1 + piece.size});
                column += 1 + static_cast<std::int64_t>(piece.size);
            } else {
                rest.push_back({.term = pieces_.cons(pieces_.cons(pieces_.integer(','), indent.term), piece.term),
                                .size = 1 + indent.size + piece.size});
                column = static_cast<std::int64_t>(1 + indent.size + piece.size) - 2;
            }
        });
        const std::array parts{byte('['), *head, sum(rest), byte(']')};
        return sum(parts);
    }

    // Members [$,, Indent, Key, ": " | Value] in key order, then OTP's format_object.
    Piece object(const Term &map, const Layout &layout, int depth) {
        const auto entries = need(map.map_entries());
        if (entries.empty()) {
            return pieces_.binary("{}");
        }
        const auto inner = nested(layout);
        const auto indent = line_break(inner);
        std::vector<Piece> members;
        for (const auto &[name, member] : entries) {
            const auto encoded = key(pieces_, name);
            const auto column = inner.level * inner.indent + 2 + static_cast<std::int64_t>(encoded.size);
            const auto piece = value(member, at(inner, column), depth);
            const auto colon = pieces_.chars(": ");
            members.push_back({.term = pieces_.cons(encoded.term, pieces_.cons(colon.term, piece.term)),
                               .size = encoded.size + 2 + piece.size});
        }
        const auto closing = line_break(layout);
        if (members.size() == 1) {
            const auto &only = entries[0].second;
            if (only.is_list() || only.is_map()) {
                const std::array parts{byte('{'), indent, members[0], closing, byte('}')};
                return sum(parts);
            }
            const std::array parts{pieces_.chars("{ "), members[0], pieces_.chars(" }")};
            return sum(parts);
        }
        std::vector<Piece> rest;
        for (std::size_t index = 1; index < members.size(); ++index) {
            rest.push_back({.term = pieces_.cons(pieces_.integer(','), pieces_.cons(indent.term, members[index].term)),
                            .size = 1 + indent.size + members[index].size});
        }
        const std::array parts{byte('{'), indent, members[0], sum(rest), closing, byte('}')};
        return sum(parts);
    }

    Pieces pieces_;
};

Term answer(ProcessContext &context, const std::optional<Term> &iodata) {
    TermFactory factory(context);
    if (!iodata) {
        return need(factory.atom("fallback"));
    }
    const std::array items{need(factory.atom("ok")), *iodata};
    return need(factory.tuple(items));
}

// clause_json:encode(Term): {ok, Iodata}, or `fallback` for json.erl to encode it.
Term encode1(ProcessContext &context, const Value &value) {
    try {
        return answer(context, Encoder(context).value(value.term, 0));
    } catch (const Fallback &) {
        return answer(context, std::nullopt);
    }
}

// clause_json:format(Term, State): {ok, Iodata} with the state's level, column, indent and max, or `fallback`.
Term format2(ProcessContext &context, const Value &value, const FormatState &state) {
    try {
        const Layout layout{field(context, state.term, "level"), field(context, state.term, "col"),
                            field(context, state.term, "indent"), field(context, state.term, "max")};
        return answer(context, Formatter(context).value(value.term, layout, 0).term);
    } catch (const Fallback &) {
        return answer(context, std::nullopt);
    }
}

constexpr std::array JSON_ENCODE_BUILTINS{
    typed_entry<encode1>("clause_json", "encode"),
    typed_entry<format2>("clause_json", "format"),
};
} // namespace
} // namespace clause::runtime::builtins::encoding

namespace clause::runtime {
std::span<const BuiltinEntry> json_encode_builtins() noexcept { return builtins::encoding::JSON_ENCODE_BUILTINS; }
} // namespace clause::runtime
