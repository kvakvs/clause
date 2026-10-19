#include "../io_format.hpp"
#include "../string/string.hpp"
#include "../text.hpp"
#include "../unicode/code_points.hpp"
#include "format.hpp"
#include "pretty.hpp"
#include "write.hpp"
#include <algorithm>

// io_lib_format's build/2 (plan 21 step LIB4C): build_small replaces the small controls by their flattened text,
// count_small shares the characters left by chars_limit between the others, and build_limited writes them as the
// deep lists of string/6, term/6 and io_lib:write/5.
namespace clause::runtime::builtins::io_lib {
namespace {
using string::Terms;

// sub/2: a limit less what was used, never below zero; negative limits stay.
std::int64_t limit_sub(std::int64_t limit, std::int64_t used) {
    return limit < 0 ? limit : limit >= used ? limit - used : 0;
}

// The arguments a control takes.
const Term &arg(const Spec &spec, std::size_t index) {
    if (index >= spec.args.size()) {
        no_clause();
    }
    return spec.args[index];
}

// One element of build_small's result: a cell, or a control left for build_limited.
using Small = std::variant<Deep::Ref, const Spec *>;

// io_lib_format:count_small/1's counts.
struct Counts {
    std::int64_t prints = 0;
    std::int64_t limited = 0;
    std::int64_t other = 0;
};

class Builder final {
  public:
    Builder(ProcessContext &context, Deep &deep) : context_(context), deep_(deep), terms_(context) {}

    // build_small/1.
    std::vector<Small> small(const std::vector<Element> &elements) {
        std::vector<Small> out;
        for (const auto &element : elements) {
            if (const auto *spec = std::get_if<Spec>(&element)) {
                if (const auto text = control_small(*spec)) {
                    flatten_into(*text, out);
                } else {
                    out.emplace_back(spec);
                }
            } else {
                out.emplace_back(cell(std::get<Term>(element)));
            }
        }
        return out;
    }

    // count_small/1.
    Counts count(const std::vector<Small> &items) const {
        Counts counts;
        for (const auto &item : items) {
            if (const auto *const *spec = std::get_if<const Spec *>(&item)) {
                if (!std::u32string_view(U"pPwWs").contains((*spec)->control)) {
                    no_clause();
                }
                counts.prints += is_print(**spec) ? 1 : 0;
                ++counts.limited;
            } else {
                counts.other += item_length(std::get<Deep::Ref>(item));
            }
        }
        return counts;
    }

    // build_limited/5.
    Deep::Ref limited(const std::vector<Small> &items, Counts counts, std::int64_t remaining, bool binary) {
        std::vector<Deep::Ref> out;
        out.reserve(items.size());
        Limits limits{.remaining = remaining, .count = counts.limited, .prints = counts.prints};
        for (const auto &item : items) {
            if (const auto *const *spec = std::get_if<const Spec *>(&item)) {
                out.push_back(binary && is_print(**spec) ? printed_bin(**spec, limits) : limited_spec(**spec, limits));
            } else {
                const auto ref = std::get<Deep::Ref>(item);
                limits.column = literal_column(ref, limits.column, binary);
                out.push_back(ref);
            }
        }
        return deep_.list(out);
    }

    // A literal element or pad: a character cell, else the term itself.
    Deep::Ref cell(const Term &term) {
        const auto code = string::small_of(term);
        return code && *code >= 0 && *code <= 0x7FFFFFFF ? deep_.code(static_cast<char32_t>(*code)) : deep_.term(term);
    }

  private:
    // What build_limited/5 carries from element to element.
    struct Limits {
        // The column the next element starts at, kept while ~p controls remain.
        std::int64_t column = 0;
        // Characters left by chars_limit (negative: no limit), shared by the `count` limited controls left.
        std::int64_t remaining = -1;
        std::int64_t count = 0;
        std::int64_t prints = 0;
    };

    // What a limited control is written within: its depth (-1: none), share of chars_limit (-1: none) and column.
    struct Bounds {
        std::int64_t depth = -1;
        std::int64_t limit = -1;
        std::int64_t column = 0;
    };

    // Whether a control is ~p or ~P.
    static bool is_print(const Spec &spec) { return spec.control == 'p' || spec.control == 'P'; }

    // One limited control of build_limited/5, with its share of the characters left.
    Deep::Ref limited_spec(const Spec &spec, Limits &limits) {
        const auto max_chars = limits.remaining < 0 ? limits.remaining : limits.remaining / limits.count;
        const auto text = control_limited(spec, {.depth = -1, .limit = max_chars, .column = limits.column});
        limits.prints -= is_print(spec) ? 1 : 0;
        --limits.count;
        if (limits.remaining >= 0) {
            limits.remaining = limit_sub(limits.remaining, deep_.chars_length(context_, text));
        }
        if (limits.prints > 0) {
            limits.column = deep_.indentation(context_, text, limits.column);
        }
        return text;
    }

    // build_limited_bin/5 at ~p or ~P: print_bin/2's bytes, its size taken off chars_limit and its column (or the
    // indentation of its text, for a negative one) carried to the next ~p.
    Deep::Ref printed_bin(const Spec &spec, Limits &limits) {
        const auto max_chars = limits.remaining < 0 ? limits.remaining : limits.remaining / limits.count;
        const auto depth = spec.control == 'P' ? depth_of(arg(spec, 1)) : -1;
        const auto result = pretty_print_bin(
            context_, arg(spec, 0), print_options(spec, {.depth = depth, .limit = max_chars, .column = limits.column}));
        const auto column = result.column > 0 ? result.column : limits.column - result.column;
        const auto text = deep_.term(terms_.binary(std::as_bytes(std::span(result.bytes))));
        --limits.prints;
        --limits.count;
        if (limits.remaining >= 0) {
            limits.remaining = limit_sub(limits.remaining, result.size);
        }
        limits.column = limits.prints > 0 && column < 0 ? deep_.indentation(context_, text, limits.column) : column;
        return text;
    }

    // The column after a literal: newlines restart it, tabs go to the next multiple of 8.
    std::int64_t literal_column(Deep::Ref ref, std::int64_t column, bool binary) const {
        const auto code = deep_.kind(ref) == Deep::Kind::code ? deep_.code_of(ref) : U'\0';
        column = code == '\n' ? 0 : code == '\t' ? (column + 8) / 8 * 8 : column + 1;
        return binary && deep_.kind(ref) == Deep::Kind::term ? binary_column(deep_.term_of(ref), column) : column;
    }

    // build_limited_bin/5's column after a literal: a binary's bytes, a list starting a line; nothing else.
    static std::int64_t binary_column(const Term &literal, std::int64_t column) {
        if (literal.is_binary()) {
            return column - 1 + static_cast<std::int64_t>(string::binary_bytes(literal).size());
        }
        if (literal.is_cons() && string::is_char(literal.head_unchecked(), '\n')) {
            return 0;
        }
        if (!literal.is_integer()) {
            no_clause();
        }
        return column;
    }

    // count_small's length of one element: 1 for a character, chars_length of a list, string:length of a binary.
    std::int64_t item_length(Deep::Ref ref) const {
        if (deep_.kind(ref) == Deep::Kind::code) {
            return 1;
        }
        const auto &term = deep_.term_of(ref);
        if (term.is_list()) {
            return term_chars_length(context_, term);
        }
        if (term.is_binary()) {
            return *string::small_of(string::length_of(context_, term));
        }
        if (!term.is_integer()) {
            no_clause();
        }
        return 1;
    }

    // lists:flatten/1 of a small control's text, appended as elements.
    void flatten_into(Deep::Ref ref, std::vector<Small> &out) {
        for (; deep_.kind(ref) == Deep::Kind::cons; ref = deep_.tail(ref)) {
            flatten_into(deep_.head(ref), out);
        }
        if (deep_.kind(ref) == Deep::Kind::code) {
            out.emplace_back(ref);
        } else if (deep_.kind(ref) == Deep::Kind::term) {
            flatten_term(deep_.term_of(ref), out);
        }
    }

    // A term within a flattened text: its elements when it is a list, else itself.
    void flatten_term(const Term &term, std::vector<Small> &out) {
        if (!term.is_list()) {
            out.emplace_back(cell(term));
            return;
        }
        Term rest = term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            flatten_term(rest.head_unchecked(), out);
        }
        if (!rest.is_nil()) {
            bad_argument();
        }
    }

    // chars/2: a character repeated as OTP's balanced deep list.
    Deep::Ref chars(Deep::Ref c, std::int64_t n) {
        if (n < 0) {
            bad_argument();
        }
        if (n <= 3) {
            const std::array copies{c, c, c};
            return deep_.list(std::span(copies).first(static_cast<std::size_t>(n)));
        }
        const auto half = chars(c, n >> 1);
        return (n & 1) == 0 ? deep_.cons(half, half) : deep_.cons(c, deep_.cons(half, half));
    }

    Deep::Ref repeated(char32_t c, std::int64_t n) { return chars(deep_.code(c), n); }

    // adjust/3.
    Deep::Ref adjust(Deep::Ref data, Deep::Ref pad, bool left) {
        if (pad == Deep::NIL) {
            return data;
        }
        return left ? deep_.cons(data, pad) : deep_.cons(pad, data);
    }

    // term/6: text within the field, or '*' characters when it does not fit.
    Deep::Ref term_field(Deep::Ref text, const Spec &spec, std::optional<std::int64_t> precision, bool unicode) {
        if (!spec.width && !precision) {
            return text;
        }
        const auto width = spec.width ? *spec.width : *precision;
        const auto length = unicode ? *string::small_of(string::length_of(context_, deep_.to_term(context_, text)))
                                    : deep_.chars_length(context_, text);
        const auto room = std::min(length, precision ? std::min(*precision, width) : width);
        const auto pad = cell(spec.pad);
        if (length > room) {
            return adjust(repeated('*', room), chars(pad, width - room), spec.left);
        }
        return adjust(text, chars(pad, width - length), spec.left);
    }

    // flat_trunc/3: the first `n` characters, flattened (Latin-1) or by string:slice/3 (Unicode).
    Deep::Ref truncated(Deep::Ref text, std::int64_t n, bool unicode) {
        if (unicode) {
            return deep_.term(string::prefix_of(context_, deep_.to_term(context_, text), n));
        }
        std::vector<Small> flat;
        flatten_into(text, flat);
        if (static_cast<std::int64_t>(flat.size()) < n) {
            bad_argument();
        }
        std::vector<Deep::Ref> kept;
        kept.reserve(static_cast<std::size_t>(n));
        for (std::int64_t i = 0; i < n; ++i) {
            kept.push_back(std::get<Deep::Ref>(flat[static_cast<std::size_t>(i)]));
        }
        return deep_.list(kept);
    }

    // string_field/6.
    Deep::Ref string_field(Deep::Ref text, std::int64_t width, bool left, std::int64_t length, const Spec &spec) {
        if (length > width) {
            return truncated(text, width, spec.unicode);
        }
        return length < width ? adjust(text, chars(cell(spec.pad), width - length), left) : text;
    }

    // string/6: precision characters (truncated or padded) within the field width.
    Deep::Ref string_text(Deep::Ref text, std::optional<std::int64_t> width, const Spec &spec) {
        const auto &precision = spec.precision;
        if (!width && !precision) {
            return text;
        }
        const auto length = deep_.chars_length(context_, text);
        if (!precision) {
            return string_field(text, *width, spec.left, length, spec);
        }
        if (!width) {
            return string_field(text, *precision, true, length, spec);
        }
        if (*width < *precision) {
            no_clause();
        }
        if (*width == *precision) {
            return string_field(text, *width, spec.left, length, spec);
        }
        const auto pad = cell(spec.pad);
        const auto outer = chars(pad, *width - *precision);
        if (length > *precision) {
            return adjust(truncated(text, *precision, spec.unicode), outer, spec.left);
        }
        if (length < *precision) {
            return adjust(deep_.cons(text, chars(pad, *precision - length)), outer, spec.left);
        }
        return adjust(text, outer, spec.left);
    }

    // char/5.
    Deep::Ref char_field(Deep::Ref c, const Spec &spec) {
        const auto width = spec.width;
        const auto precision = spec.precision;
        if (!width && !precision) {
            return deep_.list({c});
        }
        if (!width || !precision) {
            return chars(c, width ? *width : *precision);
        }
        if (*width < *precision) {
            no_clause();
        }
        return adjust(chars(c, *precision), chars(cell(spec.pad), *width - *precision), spec.left);
    }

    // The digits of |value| in `base` (2..36), lowercase for ~b, ~x and ~+.
    std::string digits_of(const Term &value, std::int64_t base, bool lowercase) {
        if (base < 2 || base > 36) {
            no_clause();
        }
        auto number = need(detail::integer_read(value));
        auto text = integer_digits(number < 0 ? detail::Integer{-number} : number, static_cast<unsigned>(base));
        if (lowercase) {
            std::ranges::transform(text, text.begin(), [](char c) { return c >= 'A' ? static_cast<char>(c + 32) : c; });
        }
        return text;
    }

    // unprefixed_integer/6 and prefixed_integer/7.
    Deep::Ref integer_field(const Spec &spec, std::optional<Deep::Ref> prefix, bool lowercase) {
        const auto &value = arg(spec, 0);
        auto text = deep_.ascii(digits_of(value, spec.precision.value_or(10), lowercase));
        if (prefix) {
            text = deep_.cons(*prefix, text);
        }
        if (need(detail::integer_read(value)) < 0) {
            text = deep_.cons(deep_.code('-'), text);
        }
        return term_field(text, spec, std::nullopt, false);
    }

    // The prefix of ~x and ~X: an atom's characters, or a deep character list as it is.
    Deep::Ref prefix_of(const Term &prefix) {
        if (prefix.is_atom()) {
            return deep_.text(code_points(prefix.atom_spelling_unchecked()));
        }
        if (!deep_list_of(prefix, unicode_char)) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = terms_.boolean(false).word()};
        }
        return deep_.term(prefix);
    }

    // The ~e, ~f and ~g text in its field (term/6 with the width as precision).
    std::optional<Deep::Ref> float_field(const Spec &spec) {
        const auto value = need(arg(spec, 0).float_value());
        auto precision = spec.precision.value_or(6);
        auto control = spec.control;
        if (control == 'g') {
            if (precision < 1) {
                no_clause();
            }
            const auto choice = float_g({.value = value, .precision = precision});
            control = choice.fixed ? 'f' : 'e';
            precision = choice.precision;
        }
        const auto text = control == 'e' ? float_e({.value = value, .precision = precision})
                                         : float_f({.value = value, .precision = precision});
        if (!text) {
            no_clause();
        }
        return term_field(deep_.ascii(*text), spec, spec.width, false);
    }

    // ~s of an atom: its characters (Latin-1 only without t) within the field.
    Deep::Ref atom_string(const Spec &spec) {
        const auto text = code_points(need(arg(spec, 0).atom_spelling()));
        if (!spec.unicode && std::ranges::any_of(text, [](char32_t c) { return c > 0xFF; })) {
            no_clause();
        }
        return string_text(deep_.text(text), spec.width, spec);
    }

    // control_small/7: the text of a small control, none for the others.
    std::optional<Deep::Ref> control_small(const Spec &spec) {
        const auto c = spec.control;
        if (!std::u32string_view(U"sefgbBxX+#c").contains(c)) {
            return other_small(spec);
        }
        if (!small_argument(spec)) {
            return std::nullopt;
        }
        if (c == 's') {
            return atom_string(spec);
        }
        return std::u32string_view(U"efg").contains(c) ? float_field(spec) : std::optional{integer_small(spec)};
    }

    // Whether a control's first argument is what its small form takes: an atom for ~s, a float for ~e, ~f and ~g,
    // an integer for the others.
    static bool small_argument(const Spec &spec) {
        if (spec.args.empty()) {
            return false;
        }
        const auto &first = spec.args.front();
        switch (spec.control) {
        case 's':
            return first.is_atom();
        case 'e':
        case 'f':
        case 'g':
            return first.is_float();
        default:
            return first.is_integer();
        }
    }

    // The integer controls ~b, ~B, ~x, ~X, ~+, ~# and ~c of an integer.
    Deep::Ref integer_small(const Spec &spec) {
        switch (spec.control) {
        case 'b':
        case 'B':
            return integer_field(spec, std::nullopt, spec.control == 'b');
        case 'x':
        case 'X':
            return integer_field(spec, prefix_of(arg(spec, 1)), spec.control == 'x');
        case '+':
        case '#':
            return integer_field(spec, base_prefix(spec), spec.control == '+');
        default:
            return char_field(character(spec), spec);
        }
    }

    // ~~, ~n and ~i.
    std::optional<Deep::Ref> other_small(const Spec &spec) {
        switch (spec.control) {
        case '~':
            return char_field(deep_.code('~'), spec);
        case 'n':
            if (!spec.width) {
                return deep_.ascii("\n");
            }
            if (spec.left) {
                no_clause();
            }
            return repeated('\n', *spec.width);
        case 'i':
            return Deep::NIL;
        default:
            return std::nullopt;
        }
    }

    // The [integer_to_list(Base), $#] prefix of ~+ and ~#.
    Deep::Ref base_prefix(const Spec &spec) {
        return deep_.list({deep_.ascii(std::to_string(spec.precision.value_or(10))), deep_.code('#')});
    }

    // The character of ~c: its low byte without t.
    Deep::Ref character(const Spec &spec) {
        const auto &value = arg(spec, 0);
        if (spec.unicode) {
            return cell(value);
        }
        const auto number = need(detail::integer_read(value));
        const detail::Integer byte = (number % 256 + 256) % 256;
        return deep_.code(byte.convert_to<char32_t>());
    }

    // control_limited/11 with the control's share of chars_limit (`bounds.limit`) at column `bounds.column`.
    Deep::Ref control_limited(const Spec &spec, Bounds bounds) {
        if (spec.control == 'W' || spec.control == 'P') {
            bounds.depth = depth_of(arg(spec, 1));
        }
        switch (spec.control) {
        case 's':
            return limited_string(spec, bounds.limit);
        case 'w':
        case 'W':
            return term_field(written(spec, arg(spec, 0), bounds), spec, spec.precision, spec.unicode);
        case 'p':
        case 'P':
            return printed(spec, bounds);
        default:
            no_clause();
        }
    }

    // The depth argument of ~W and ~P.
    static std::int64_t depth_of(const Term &depth) {
        const auto value = string::small_of(depth);
        if (!depth.is_integer() || !value) {
            no_clause();
        }
        return *value;
    }

    // io_lib:write/5 with the control's encoding and order: write1/4, or intermediate/7 with a chars_limit share.
    Deep::Ref written(const Spec &spec, const Term &term, const Bounds &bounds) {
        if (bounds.depth == 0 || bounds.limit == 0) {
            return deep_.ascii("...");
        }
        const auto encoding = spec.unicode ? Encoding::unicode : Encoding::latin1;
        if (bounds.limit > 0) {
            return limited_write(context_, deep_, term,
                                 {.encoding = encoding,
                                  .order = order_of(spec.order),
                                  .depth = bounds.depth,
                                  .chars_limit = bounds.limit});
        }
        Writer writer(context_, deep_, encoding, order_of(spec.order));
        return writer.write(term, bounds.depth);
    }

    // print/12's io_lib_pretty options: the line length F (80), the column P (the current one), the depth and the
    // control's share of chars_limit.
    static PrettyOptions print_options(const Spec &spec, const Bounds &bounds) {
        if (spec.left) {
            no_clause();
        }
        return {.column = spec.precision.value_or(bounds.column + 1),
                .line_length = spec.width.value_or(80),
                .depth = bounds.depth,
                .chars_limit = bounds.limit,
                .encoding = spec.unicode ? Encoding::unicode : Encoding::latin1,
                .strings = spec.strings,
                .order = order_of(spec.order)};
    }

    // print/12: ~p and ~P as io_lib_pretty:print/2 lays them out (pad characters are ignored).
    Deep::Ref printed(const Spec &spec, const Bounds &bounds) {
        return pretty_print(context_, deep_, arg(spec, 0), print_options(spec, bounds));
    }

    // ~s: the chardata as characters, limited to the share of chars_limit, within the field.
    Deep::Ref limited_string(const Spec &spec, std::int64_t limit) {
        const auto &data = arg(spec, 0);
        const bool whole = limit < 0 || (spec.width && limit >= *spec.width);
        const auto text = spec.unicode ? (whole ? cdata_chars(data, 0) : limited_cdata(data, limit_sub(limit, 3)))
                                       : (whole ? iolist_chars(data, 0) : limited_iolist(data, limit_sub(limit, 3)));
        const auto width = limit < 0 || !spec.width
                               ? spec.width
                               : std::optional{std::max<std::int64_t>(3, std::min(*spec.width, limit))};
        return string_text(text, width, spec);
    }

    // binary_to_list/1 as cells.
    Deep::Ref bytes_of(const Term &binary, std::size_t from, std::size_t count) {
        if (!binary.is_binary()) {
            bad_argument();
        }
        const auto bytes = string::binary_bytes(binary).subspan(from, count);
        std::u32string codes;
        std::ranges::transform(bytes, std::back_inserter(codes),
                               [](std::byte b) { return std::to_integer<char32_t>(b); });
        return deep_.text(codes);
    }

    Deep::Ref bytes_of(const Term &binary) {
        return bytes_of(binary, 0, binary.is_binary() ? string::binary_bytes(binary).size() : 0);
    }

    // iolist_to_chars/1: Latin-1 characters kept, nested lists converted, binaries as their bytes.
    Deep::Ref iolist_chars(const Term &data, std::size_t level) {
        if (level > MAX_NESTING) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::system_limit};
        }
        if (data.is_bitstring()) {
            return bytes_of(data);
        }
        std::vector<Deep::Ref> items;
        Term rest = data;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto head = rest.head_unchecked();
            const auto code = string::small_of(head);
            items.push_back(code && *code >= 0 && *code <= 0xFF ? deep_.code(static_cast<char32_t>(*code))
                                                                : iolist_chars(head, level + 1));
        }
        if (!rest.is_nil() && !rest.is_bitstring()) {
            no_clause();
        }
        return deep_.list(items, rest.is_nil() ? Deep::NIL : bytes_of(rest));
    }

    // cdata_to_chars/1: characters kept, nested lists converted, binaries as UTF-8 (else their bytes).
    Deep::Ref cdata_chars(const Term &data, std::size_t level) {
        if (level > MAX_NESTING) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::system_limit};
        }
        if (data.is_bitstring()) {
            return utf8_or_bytes(data);
        }
        std::vector<Deep::Ref> items;
        Term rest = data;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto head = rest.head_unchecked();
            const bool character = head.is_integer() && need(detail::integer_read(head)) >= 0;
            items.push_back(character ? cell(head) : cdata_chars(head, level + 1));
        }
        if (!rest.is_nil() && !rest.is_bitstring()) {
            no_clause();
        }
        return deep_.list(items, rest.is_nil() ? Deep::NIL : utf8_or_bytes(rest));
    }

    // A binary's UTF-8 characters, or its bytes when it is not UTF-8.
    Deep::Ref utf8_or_bytes(const Term &binary) {
        if (binary.is_binary()) {
            if (const auto text = decode_utf8(string::binary_bytes(binary))) {
                return deep_.text(*text);
            }
        }
        return bytes_of(binary);
    }

    // limit_iolist_to_chars/4 from its start in mode normal.
    Deep::Ref limited_iolist(const Term &data, std::int64_t limit) { return limit_iolist({data}, limit, false); }

    // limit_iolist_to_chars/4's state: the data at hand, what follows it, the characters left and those taken.
    struct IolistWalk {
        Term cs;
        std::vector<Term> stack;
        std::int64_t limit = 0;
        std::vector<Deep::Ref> items;
    };

    Deep::Ref limit_iolist(std::vector<Term> stack, std::int64_t limit, bool final) {
        IolistWalk walk{.cs = stack.back(), .stack = std::move(stack), .limit = limit, .items = {}};
        walk.stack.pop_back();
        while (walk.limit != 0) {
            if (!iolist_step(walk)) {
                return deep_.list(walk.items);
            }
        }
        return deep_.list(walk.items, final ? Deep::NIL : limit_end(walk.cs, walk.stack));
    }

    // One step of limit_iolist_to_chars/4; false once the data ends.
    bool iolist_step(IolistWalk &walk) {
        auto &cs = walk.cs;
        if (cs.is_cons()) {
            const auto head = cs.head_unchecked();
            const auto code = string::small_of(head);
            if (code && *code >= 0 && *code <= 0xFF) {
                walk.items.push_back(deep_.code(static_cast<char32_t>(*code)));
                --walk.limit;
                cs = cs.tail_unchecked();
            } else {
                walk.stack.push_back(cs.tail_unchecked());
                cs = head;
            }
            return true;
        }
        if (cs.is_nil()) {
            if (walk.stack.empty()) {
                return false;
            }
            cs = walk.stack.back();
            walk.stack.pop_back();
            return true;
        }
        if (!cs.is_binary()) {
            no_clause();
        }
        iolist_binary(walk);
        return true;
    }

    // limit_iolist_to_chars/4 at a binary: its bytes up to the limit as one list, the rest left for later.
    void iolist_binary(IolistWalk &walk) {
        const auto size = static_cast<std::int64_t>(string::binary_bytes(walk.cs).size());
        const auto taken = std::min(size, walk.limit);
        walk.items.push_back(bytes_of(walk.cs, 0, static_cast<std::size_t>(taken)));
        walk.cs = size > walk.limit ? Terms(context_).slice(walk.cs, static_cast<std::size_t>(taken),
                                                            static_cast<std::size_t>(size - taken))
                                    : Terms(context_).nil();
        walk.limit = size > walk.limit ? 0 : walk.limit - size;
    }

    // The rest of a limited ~s once its share runs out: what is left when it is under 4 characters, else "...".
    Deep::Ref limit_end(const Term &cs, std::vector<Term> stack) {
        stack.push_back(cs);
        const auto rest = limit_iolist(std::move(stack), 4, true);
        const auto size = deep_.chars_length(context_, rest);
        return size < 4 ? rest : deep_.ascii("...");
    }

    // limit_cdata_to_chars/3 in mode normal.
    Deep::Ref limited_cdata(const Term &data, std::int64_t limit) { return limit_cdata(data, limit, false); }

    Deep::Ref limit_cdata(Term cs, std::int64_t limit, bool final) {
        std::vector<Deep::Ref> items;
        for (; limit != 0; --limit) {
            if (!cdata_step(cs, items)) {
                return deep_.list(items);
            }
        }
        if (final) {
            return deep_.list(items);
        }
        const auto rest = limit_cdata(cs, 4, true);
        const auto size = *string::small_of(string::length_of(context_, deep_.to_term(context_, rest)));
        return deep_.list(items, size < 4 ? rest : deep_.ascii("..."));
    }

    // One cluster of limit_cdata_to_chars/3 (string:next_grapheme/1) taken into `items`; false at the end.
    bool cdata_step(Term &cs, std::vector<Deep::Ref> &items) {
        const auto next = unicode::gc_term(context_, cs);
        if (next.is_nil()) {
            return false;
        }
        if (!next.is_tuple()) {
            items.push_back(cell(need(next.head())));
            cs = need(next.tail());
            return true;
        }
        // {error, <<C, Rest/binary>>} or {error, [C | Rest]}: C taken as it is (Latin-1 binaries under ~ts).
        const auto rest = need(next.tuple_element(1));
        if (rest.is_binary() && !string::binary_bytes(rest).empty()) {
            items.push_back(deep_.code(std::to_integer<char32_t>(string::binary_bytes(rest)[0])));
            cs = Terms(context_).slice(rest, 1, string::binary_bytes(rest).size() - 1);
        } else if (rest.is_cons()) {
            items.push_back(cell(rest.head_unchecked()));
            cs = rest.tail_unchecked();
        } else {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = next.word()};
        }
        return true;
    }

    ProcessContext &context_;
    Deep &deep_;
    Terms terms_;
};
} // namespace

MapsOrder order_of(const Term &order) {
    if (order.is_function(2)) {
        return {MapsOrder::sorted, order.word()};
    }
    const auto name = order.is_atom() ? order.atom_spelling_unchecked() : std::string_view{};
    return name == "undefined"  ? MapsOrder::undefined
           : name == "ordered"  ? MapsOrder::ordered
           : name == "reversed" ? MapsOrder::reversed
                                : MapsOrder::other;
}

Deep::Ref build(ProcessContext &context, Deep &deep, const std::vector<Element> &elements, const Term &limit,
                bool binary) {
    Builder builder(context, deep);
    const auto items = builder.small(elements);
    const auto counts = builder.count(items);
    std::vector<Deep::Ref> plain;
    if (counts.limited == 0) {
        for (const auto &item : items) {
            plain.push_back(std::get<Deep::Ref>(item));
        }
        return deep.list(plain);
    }
    const auto chars = string::small_of(limit);
    if (!chars) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
    }
    return builder.limited(items, counts, limit_sub(*chars, counts.other), binary);
}
} // namespace clause::runtime::builtins::io_lib
