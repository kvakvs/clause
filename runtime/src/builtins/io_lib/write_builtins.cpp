#include "../string/string.hpp"
#include "../text.hpp"
#include "../typed.hpp"
#include "format.hpp"
#include "pretty.hpp"
#include "write.hpp"
#include <array>
#include <terms.hpp>

// io_lib's writers and checks as builtins (plan 21 step LIB4C, docs/builtins.md#io-lib): write/1,2,5, bwrite/2,
// write_bin/5, write_binary/3, write_binary_bin/4, the atom, string and character writers, the character list
// checks, chars_length/1, indentation/2, nl/0 and limit_term/2.
namespace clause::runtime::builtins::io_lib {
namespace {
using string::Terms;

// The roles of term arguments that would be easily swapped (typed.hpp's Named).
struct DepthRole;
struct LimitRole;
using DepthArg = Named<DepthRole>;
using LimitArg = Named<LimitRole>;
struct ColumnRole;
using ColumnArg = Named<ColumnRole>;
struct LineRole;
using LineArg = Named<LineRole>;

// write1/4's encoding: latin1 escapes atoms, anything else keeps them.
Encoding encoding_of(const Term &encoding) {
    return encoding.is_atom() && encoding.atom_spelling_unchecked() == "latin1" ? Encoding::latin1 : Encoding::unicode;
}

// A small integer argument, or none.
std::optional<std::int64_t> small_value(const Term &term) { return string::small_of(term); }

// io_lib:write/5's choice: "..." for depth or chars_limit 0 (depth 0), write1/4 with a depth when chars_limit is
// negative, intermediate/7 when it is above 0 (any other depth than an integer counts as unlimited there).
struct WriteChoice {
    std::int64_t depth = -1;
    bool limited = false;
    std::int64_t limit = -1;
};

WriteChoice write_choice(const Term &depth, const Term &limit) {
    if (string::is_char(depth, 0) || string::is_char(limit, 0)) {
        return {.depth = 0};
    }
    if (!limit.is_integer()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::if_clause};
    }
    const auto chars = small_value(limit);
    if (!chars) {
        bad_argument();
    }
    if (*chars > 0) {
        return {.depth = small_value(depth).value_or(-1), .limited = true, .limit = *chars};
    }
    if (!depth.is_integer()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::if_clause};
    }
    const auto value = small_value(depth);
    if (!value) {
        bad_argument();
    }
    return {.depth = *value};
}

// The options of io_lib:write/2 and bwrite/2: depth, encoding (utf8 by default), chars_limit and maps_order.
struct WriteOptions {
    Term depth;
    Term encoding;
    Term limit;
    Term order;
};

WriteOptions options_of(ProcessContext &context, const Term &options) {
    const Terms terms(context);
    return {.depth = option_of(options, "depth", terms.integer(-1)),
            .encoding = option_of(options, "encoding", terms.atom("utf8")),
            .limit = option_of(options, "chars_limit", terms.integer(-1)),
            .order = option_of(options, "maps_order", terms.atom("undefined"))};
}

Term written(ProcessContext &context, const Term &term, const WriteOptions &options) {
    Deep deep;
    const auto choice = write_choice(options.depth, options.limit);
    if (choice.depth == 0) {
        return deep.to_term(context, deep.ascii("..."));
    }
    const auto encoding = encoding_of(options.encoding);
    const auto order = order_of(options.order);
    if (choice.limited) {
        return deep.to_term(
            context,
            limited_write(context, deep, term,
                          {.encoding = encoding, .order = order, .depth = choice.depth, .chars_limit = choice.limit}));
    }
    Writer writer(context, deep, encoding, order);
    return deep.to_term(context, writer.write(term, choice.depth));
}

Term write1(ProcessContext &context, const Term &term) {
    const Terms terms(context);
    return written(context, term,
                   {.depth = terms.integer(-1),
                    .encoding = terms.atom("latin1"),
                    .limit = terms.integer(-1),
                    .order = terms.atom("undefined")});
}

// io_lib:write/2: an options list, or a depth with Latin-1 atoms.
Term write2(ProcessContext &context, const Term &term, const Term &options) {
    const Terms terms(context);
    if (options.is_list()) {
        return written(context, term, options_of(context, options));
    }
    return written(context, term,
                   {.depth = options,
                    .encoding = terms.atom("latin1"),
                    .limit = terms.integer(-1),
                    .order = terms.atom("undefined")});
}

Term write5(ProcessContext &context, const Term &term, const DepthArg &depth, const EncodingArg &encoding,
            const Term &order, const LimitArg &limit) {
    return written(context, term,
                   {.depth = depth.term, .encoding = encoding.term, .limit = limit.term, .order = order});
}

// write_bin/5's {Utf8Binary, Size}.
std::pair<Term, std::int64_t> written_bin(ProcessContext &context, const Term &term, const WriteOptions &options) {
    const Terms terms(context);
    const auto choice = write_choice(options.depth, options.limit);
    if (choice.depth == 0) {
        return {terms.utf8(U"..."), 3};
    }
    const auto encoding = encoding_of(options.encoding);
    const auto order = order_of(options.order);
    if (choice.limited) {
        const auto [bytes, size] = limited_write_bin(
            context, term, {.encoding = encoding, .order = order, .depth = choice.depth, .chars_limit = choice.limit});
        return {terms.binary(std::as_bytes(std::span(bytes))), size};
    }
    Deep deep;
    Writer writer(context, deep, encoding, order);
    const auto text = writer.write(term, choice.depth);
    return {terms.utf8(deep.flat(text)), writer.size_of(text)};
}

Term write_bin5(ProcessContext &context, const Term &term, const DepthArg &depth, const EncodingArg &encoding,
                const Term &order, const LimitArg &limit) {
    const auto [bin, size] = written_bin(
        context, term, {.depth = depth.term, .encoding = encoding.term, .limit = limit.term, .order = order});
    const Terms terms(context);
    return terms.tuple(std::array{bin, terms.integer(size)});
}

Term bwrite2(ProcessContext &context, const Term &term, const Term &options) {
    if (!options.is_list()) {
        no_clause();
    }
    return written_bin(context, term, options_of(context, options)).first;
}

// write_binary/3's {Chars, Rest}.
Term write_binary3(ProcessContext &context, const Term &bits, const DepthArg &depth, const LimitArg &limit) {
    const auto chars = small_value(limit.term);
    if (!bits.is_bitstring() || !limit.term.is_integer() || !chars) {
        no_clause();
    }
    Deep deep;
    Writer writer(context, deep, Encoding::latin1, MapsOrder::undefined);
    const auto [text, rest] = writer.binary(bits, {.depth = small_value(depth.term).value_or(-1), .limit = *chars});
    const Terms terms(context);
    return terms.tuple(std::array{deep.to_term(context, text), rest});
}

// write_binary_bin/4: the same text after Acc in a binary.
Term write_binary_bin4(ProcessContext &context, const Term &bits, const DepthArg &depth, const LimitArg &limit,
                       const Term &acc) {
    const auto chars = small_value(limit.term);
    if (!bits.is_bitstring() || !acc.is_binary() || !limit.term.is_integer() || !chars) {
        no_clause();
    }
    Deep deep;
    Writer writer(context, deep, Encoding::latin1, MapsOrder::undefined);
    const auto [text, rest] = writer.binary(bits, {.depth = small_value(depth.term).value_or(-1), .limit = *chars});
    const Terms terms(context);
    auto bytes = need(acc.binary_bytes());
    const auto tail = utf8(deep.flat(text));
    std::ranges::transform(tail, std::back_inserter(bytes), [](char c) { return static_cast<std::byte>(c); });
    return terms.tuple(std::array{terms.binary(bytes), rest});
}

Term written_atom(ProcessContext &context, const Term &atom, Encoding encoding) {
    if (!atom.is_atom()) {
        bad_argument();
    }
    Deep deep;
    return deep.to_term(context, atom_text(deep, atom, encoding));
}

Term write_atom1(ProcessContext &context, const Term &atom) { return written_atom(context, atom, Encoding::unicode); }

Term write_atom_as_latin1_1(ProcessContext &context, const Term &atom) {
    return written_atom(context, atom, Encoding::latin1);
}

Term quote_atom2(ProcessContext &context, const Term &atom, const Term &chars) {
    return Terms(context).boolean(quote_atom(atom.is_atom() ? atom.atom_spelling_unchecked() : "", chars));
}

// write_string/2 and its encodings.
template <Escape E> Term string2(ProcessContext &context, const Term &string, const QuoteArg &quote) {
    Deep deep;
    return deep.to_term(context, string_text(deep, string, E, quote.term));
}

template <Escape E> Term string1(ProcessContext &context, const Term &string) {
    return string2<E>(context, string, {Terms(context).integer('"')});
}

Term bwrite_string3(ProcessContext &context, const Term &string, const QuoteArg &quote, const EncodingArg &encoding) {
    return string_bin(context, string, quote, encoding).first;
}

Term write_string_bin3(ProcessContext &context, const Term &string, const QuoteArg &quote,
                       const EncodingArg &encoding) {
    const auto [bin, size] = string_bin(context, string, quote, encoding);
    const Terms terms(context);
    return terms.tuple(std::array{bin, terms.integer(size)});
}

// write_char/1 and its encodings: [$$ | the escaped character]; write_char/1 writes a space as $\s.
template <Escape E, bool SPACE_NAMED> Term char1(ProcessContext &context, const Term &code) {
    const auto value = small_value(code);
    const bool latin1 = E == Escape::latin1;
    if (!code.is_integer() || need(detail::integer_read(code)) < 0 || (latin1 && (!value || *value > 0xFF))) {
        no_clause();
    }
    Deep deep;
    if (SPACE_NAMED && value == ' ') {
        return deep.to_term(context, deep.ascii("$\\s"));
    }
    return deep.to_term(context, deep.cons(deep.code('$'), string_char(deep, code, E, -1, Deep::NIL)));
}

// The character ranges of the checks.

bool latin1_char(std::int64_t c) { return c >= 0 && c <= 0xFF; }

// The control characters printable lists may hold.
bool printable_control(std::int64_t c) {
    return c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\b' || c == '\f' || c == 0x1B;
}

bool printable_latin1(std::int64_t c) {
    return (c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c <= 0xFF) || printable_control(c);
}

bool printable_unicode(std::int64_t c) {
    return (c >= 0x20 && c <= 0x7E) || (c >= 0xA0 && c < 0xD800) || (c > 0xDFFF && c < 0xFFFE) ||
           (c > 0xFFFF && c <= 0x10FFFF) || printable_control(c);
}

// A flat list whose elements all pass `accept`.
template <bool (*ACCEPT)(std::int64_t)> bool flat_list(const Term &term) {
    Term rest = term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto value = small_value(rest.head_unchecked());
        if (!value || !ACCEPT(*value)) {
            return false;
        }
    }
    return rest.is_nil();
}

// A possibly deep list whose characters all pass `accept`.
template <bool (*ACCEPT)(std::int64_t)> bool deep_list(const Term &term) { return deep_list_of(term, ACCEPT); }

template <bool (*CHECK)(const Term &)> Term check1(ProcessContext &context, const Term &term) {
    return Terms(context).boolean(CHECK(term));
}

Term chars_length1(ProcessContext &context, const Term &chars) {
    return Terms(context).integer(term_chars_length(context, chars));
}

Term indentation2(ProcessContext &context, const Term &chars, const ColumnArg &column_arg) {
    const auto &column = column_arg.term;
    const auto start = small_value(column);
    if (!start) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return Terms(context).integer(term_indentation(context, chars, *start));
}

Term nl0(ProcessContext &context) { return Terms(context).list(std::array{Terms(context).integer('\n')}); }

// limit_term/2's test_limit/2 and limit/2.
class Limiter final {
  public:
    explicit Limiter(ProcessContext &context) : context_(context), terms_(context) {}

    // Whether nothing is cut at `depth` (test_limit/2 not throwing limit).
    bool fits(const Term &term, std::int64_t depth) {
        const Nested nested(level_);
        if (depth == 0) {
            return false;
        }
        if (term.is_cons()) {
            return depth != 1 && (flat_list<printable_latin1>(term) || list_fits(term, depth));
        }
        if (term.is_map()) {
            return map_fits(term, depth);
        }
        return !term.is_tuple() || tuple_fits(term, depth);
    }

    // limit/2.
    Term limit(const Term &term, std::int64_t depth) {
        const Nested nested(level_);
        if (depth == 0) {
            return dots();
        }
        if (term.is_cons()) {
            return limit_list(term, depth);
        }
        if (term.is_map()) {
            return limit_map(term, depth);
        }
        return term.is_tuple() && term.tuple_size_unchecked() > 0 ? limit_tuple(term, depth) : term;
    }

  private:
    // One level of the walk, counted while it lasts; deeper than MAX_NESTING is system_limit.
    class Nested final {
      public:
        explicit Nested(std::size_t &level) : level_(level) {
            if (++level_ > MAX_NESTING) {
                --level_;
                throw BuiltinFailure{.reason = abi::v1::ErrorReason::system_limit};
            }
        }

        ~Nested() { --level_; }

        Nested(const Nested &) = delete;
        Nested &operator=(const Nested &) = delete;

      private:
        std::size_t &level_;
    };

    Term dots() const { return terms_.atom("..."); }

    // test_limit_map_body/2: keys and values one level down, the depth falling per association (0 cuts, even after
    // the last).
    bool map_fits(const Term &term, std::int64_t depth) {
        auto at = depth;
        for (const auto &[key, value] : map_entries(term, MapsOrder::undefined)) {
            if (at == 0 || !fits(key, at - 1) || !fits(value, at - 1)) {
                return false;
            }
            --at;
        }
        return at != 0;
    }

    // test_limit_tuple/4.
    bool tuple_fits(const Term &term, std::int64_t depth) {
        auto at = depth;
        for (const auto &element : need(term.tuple_elements())) {
            if (at == 1 || !fits(element, at - 1)) {
                return false;
            }
            --at;
        }
        return true;
    }

    // test_limit_tail/2 after the head.
    bool list_fits(const Term &term, std::int64_t depth) {
        if (!fits(term.head_unchecked(), depth - 1)) {
            return false;
        }
        Term rest = term.tail_unchecked();
        for (auto at = depth - 1; !rest.is_nil(); --at) {
            if (at == 1) {
                return false;
            }
            if (!rest.is_cons()) {
                return fits(rest, at - 1);
            }
            if (!fits(rest.head_unchecked(), at - 1)) {
                return false;
            }
            rest = rest.tail_unchecked();
        }
        return true;
    }

    // limit/2 of a tuple and limit_tuple/3.
    Term limit_tuple(const Term &term, std::int64_t depth) {
        if (depth == 1) {
            return terms_.tuple(std::array{dots()});
        }
        const auto elements = need(term.tuple_elements());
        std::vector<Term> items;
        items.reserve(elements.size());
        auto at = depth - 1;
        items.push_back(limit(elements.front(), at));
        for (std::size_t i = 1; i < elements.size(); ++i, --at) {
            if (at == 1) {
                items.push_back(dots());
                break;
            }
            items.push_back(limit(elements[i], at - 1));
        }
        return terms_.tuple(items);
    }

    // limit/2 of a list and limit_tail/2.
    Term limit_list(const Term &term, std::int64_t depth) {
        if (depth == 1) {
            return terms_.list(std::array{dots()});
        }
        if (flat_list<printable_latin1>(term)) {
            return term;
        }
        std::vector<Term> items{limit(term.head_unchecked(), depth - 1)};
        Term rest = term.tail_unchecked();
        Term tail = terms_.nil();
        for (auto at = depth - 1; !rest.is_nil(); --at) {
            if (at == 1) {
                tail = terms_.list(std::array{dots()});
                break;
            }
            if (!rest.is_cons()) {
                tail = limit(rest, at - 1);
                break;
            }
            items.push_back(limit(rest.head_unchecked(), at - 1));
            rest = rest.tail_unchecked();
        }
        return terms_.list(items, tail);
    }

    // limit_map/2: the first Depth + 1 associations, values limited, keys as they are.
    Term limit_map(const Term &term, std::int64_t depth) {
        std::vector<std::pair<Term, Term>> kept;
        auto room = depth + 1;
        for (const auto &[key, value] : map_entries(term, MapsOrder::undefined)) {
            if (room == 0) {
                break;
            }
            kept.emplace_back(key, limit(value, depth - 1));
            --room;
        }
        return need(TermFactory(context_).map(kept));
    }

    ProcessContext &context_;
    Terms terms_;
    // The containers the walk is inside.
    std::size_t level_ = 0;
};

// io_lib:print/4: io_lib_pretty:print/4 with Latin-1 atoms and strings; arithmetic on a column, line length or
// depth that is not an integer raises badarith.
Term print4(ProcessContext &context, const Term &term, const ColumnArg &column, const LineArg &line,
            const DepthArg &depth) {
    const auto at = small_value(column.term);
    const auto length = small_value(line.term);
    const PrettyOptions options{.column = at.value_or(1),
                                .line_length = length.value_or(-1),
                                .depth = small_value(depth.term).value_or(NON_INTEGER_DEPTH),
                                .numeric = at && length};
    Deep deep;
    return deep.to_term(context, pretty_print(context, deep, term, options));
}

Term print1(ProcessContext &context, const Term &term) {
    Deep deep;
    return deep.to_term(context, pretty_print(context, deep, term, {}));
}

Term limit_term2(ProcessContext &context, const Term &term, const DepthArg &depth) {
    const auto value = small_value(depth.term);
    if (!value || *value < -1) {
        no_clause();
    }
    Limiter limiter(context);
    if (*value < 0 || limiter.fits(term, *value)) {
        return term;
    }
    return limiter.limit(term, *value);
}

constexpr std::array IO_LIB_WRITE_BUILTINS{
    typed_entry<print1>("io_lib", "print"),
    typed_entry<print4>("io_lib", "print"),
    typed_entry<write1>("io_lib", "write"),
    typed_entry<write2>("io_lib", "write"),
    typed_entry<write5>("io_lib", "write"),
    typed_entry<bwrite2>("io_lib", "bwrite"),
    typed_entry<write_bin5>("io_lib", "write_bin"),
    typed_entry<write_binary3>("io_lib", "write_binary"),
    typed_entry<write_binary_bin4>("io_lib", "write_binary_bin"),
    typed_entry<write_atom1>("io_lib", "write_atom"),
    typed_entry<write_atom_as_latin1_1>("io_lib", "write_atom_as_latin1"),
    typed_entry<quote_atom2>("io_lib", "quote_atom"),
    typed_entry<string1<Escape::unicode>>("io_lib", "write_string"),
    typed_entry<string2<Escape::unicode>>("io_lib", "write_string"),
    typed_entry<string1<Escape::unicode>>("io_lib", "write_unicode_string"),
    typed_entry<string1<Escape::latin1>>("io_lib", "write_latin1_string"),
    typed_entry<string2<Escape::latin1>>("io_lib", "write_latin1_string"),
    typed_entry<string1<Escape::unicode_as_latin1>>("io_lib", "write_string_as_latin1"),
    typed_entry<string2<Escape::unicode_as_latin1>>("io_lib", "write_string_as_latin1"),
    typed_entry<bwrite_string3>("io_lib", "bwrite_string"),
    typed_entry<write_string_bin3>("io_lib", "write_string_bin"),
    typed_entry<char1<Escape::unicode, true>>("io_lib", "write_char"),
    typed_entry<char1<Escape::unicode, true>>("io_lib", "write_unicode_char"),
    typed_entry<char1<Escape::latin1, false>>("io_lib", "write_latin1_char"),
    typed_entry<char1<Escape::unicode_as_latin1, false>>("io_lib", "write_char_as_latin1"),
    typed_entry<check1<flat_list<unicode_char>>>("io_lib", "char_list"),
    typed_entry<check1<flat_list<latin1_char>>>("io_lib", "latin1_char_list"),
    typed_entry<check1<deep_list<unicode_char>>>("io_lib", "deep_char_list"),
    typed_entry<check1<deep_list<unicode_char>>>("io_lib", "deep_unicode_char_list"),
    typed_entry<check1<deep_list<latin1_char>>>("io_lib", "deep_latin1_char_list"),
    typed_entry<check1<flat_list<printable_latin1>>>("io_lib", "printable_list"),
    typed_entry<check1<flat_list<printable_latin1>>>("io_lib", "printable_latin1_list"),
    typed_entry<check1<flat_list<printable_unicode>>>("io_lib", "printable_unicode_list"),
    typed_entry<chars_length1>("io_lib", "chars_length"),
    typed_entry<indentation2>("io_lib", "indentation"),
    typed_entry<nl0>("io_lib", "nl"),
    typed_entry<limit_term2>("io_lib", "limit_term"),
};
} // namespace
} // namespace clause::runtime::builtins::io_lib

namespace clause::runtime {
std::span<const BuiltinEntry> io_lib_write_builtins() noexcept { return builtins::io_lib::IO_LIB_WRITE_BUILTINS; }
} // namespace clause::runtime
