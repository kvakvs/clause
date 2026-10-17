#include "../string/string.hpp"
#include "../text.hpp"
#include "format.hpp"
#include <array>
#include <terms.hpp>

// io_lib_format's scan/2 and unscan/1 (plan 21 step LIB4C): a format and its arguments as literals and specs, the
// specs as format_spec() maps, and back.
namespace clause::runtime::builtins::io_lib {
namespace {
using string::Terms;

// Largest field width or precision accepted; larger values would only exhaust memory.
constexpr std::int64_t MAX_FIELD = std::int64_t{1} << 24;

// The elements of a format: an atom's characters, a binary's bytes, or a list's elements.
std::vector<Term> format_elements(ProcessContext &context, const Term &format) {
    const Terms terms(context);
    std::vector<Term> elements;
    if (format.is_atom()) {
        for (const auto code : code_points(format.atom_spelling_unchecked())) {
            elements.push_back(terms.integer(code));
        }
        return elements;
    }
    if (format.is_bitstring()) {
        for (const auto byte : need(format.binary_bytes())) {
            elements.push_back(terms.integer(std::to_integer<std::int64_t>(byte)));
        }
        return elements;
    }
    Term rest = format;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        elements.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return elements;
}

// Collects the control sequences of a format (io_lib_format:collect/3 and collect_cseq/2).
class Collector final {
  public:
    Collector(ProcessContext &context, std::vector<Term> format, Term arguments)
        : context_(context), format_(std::move(format)), arguments_(arguments) {}

    // Every element; badarg unless the arguments are all consumed.
    std::vector<Element> run() {
        std::vector<Element> elements;
        while (at_ < format_.size()) {
            const auto &element = format_[at_++];
            if (string::is_char(element, '~')) {
                elements.emplace_back(control());
            } else {
                elements.emplace_back(element);
            }
        }
        if (!arguments_.is_nil()) {
            bad_argument();
        }
        return elements;
    }

  private:
    // Whether the next format element is the character `c`.
    bool next_is(char32_t c, std::size_t ahead = 0) const {
        return at_ + ahead < format_.size() && string::is_char(format_[at_ + ahead], c);
    }

    // The next argument; no clause when none is left.
    Term argument() {
        if (!arguments_.is_cons()) {
            no_clause();
        }
        auto head = arguments_.head_unchecked();
        arguments_ = arguments_.tail_unchecked();
        return head;
    }

    // field_value/2: '*' with an integer argument, decimal digits, or none.
    std::optional<std::int64_t> field_value() {
        if (next_is('*') && arguments_.is_cons() && arguments_.head_unchecked().is_integer()) {
            ++at_;
            const auto value = string::small_of(argument());
            if (!value || *value < -MAX_FIELD || *value > MAX_FIELD) {
                bad_argument();
            }
            return value;
        }
        std::optional<std::int64_t> value;
        for (auto digit = digit_at(); digit; digit = digit_at()) {
            ++at_;
            value = value.value_or(0) * 10 + *digit;
            if (*value > MAX_FIELD) {
                bad_argument();
            }
        }
        return value;
    }

    // The digit the next format element is, if it is one.
    std::optional<std::int64_t> digit_at() const {
        if (at_ == format_.size()) {
            return std::nullopt;
        }
        const auto code = string::small_of(format_[at_]);
        return code && *code >= '0' && *code <= '9' ? std::optional{*code - '0'} : std::nullopt;
    }

    // field_width/2: a negative value (or '-') adjusts left; '-' without a value is badarith.
    void width(Spec &spec) {
        const bool minus = next_is('-');
        at_ += minus ? 1 : 0;
        const auto value = field_value();
        if (minus && !value) {
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::badarith};
        }
        const auto signed_width = minus ? std::optional{-*value} : value;
        spec.left = signed_width && *signed_width < 0;
        spec.width = signed_width ? std::optional{spec.left ? -*signed_width : *signed_width} : std::nullopt;
    }

    // precision/2 and pad_char/2: ".P" and ".Pad", '*' taking the pad from the arguments when there is one.
    void precision_and_pad(Spec &spec) {
        if (next_is('.')) {
            ++at_;
            spec.precision = field_value();
        }
        spec.pad = Terms(context_).integer(' ');
        if (next_is('.') && next_is('*', 1) && arguments_.is_cons()) {
            at_ += 2;
            spec.pad = argument();
        } else if (next_is('.') && at_ + 1 < format_.size()) {
            spec.pad = format_[at_ + 1];
            at_ += 2;
        }
    }

    // modifiers/3: t, l, k, and K with its ordering argument.
    void modifiers(Spec &spec) {
        const Terms terms(context_);
        while (true) {
            if (next_is('t')) {
                spec.unicode = true;
            } else if (next_is('l')) {
                spec.strings = false;
            } else if (next_is('k')) {
                spec.order = terms.atom("ordered");
            } else if (next_is('K') && arguments_.is_cons()) {
                ++at_;
                spec.order = argument();
                continue;
            } else {
                return;
            }
            ++at_;
        }
    }

    // collect_cc/2: the control character and the arguments it takes.
    void control_character(Spec &spec) {
        const auto code = at_ < format_.size() ? string::small_of(format_[at_]) : std::nullopt;
        const auto c = code && *code > 0 && *code <= 0x10FFFF ? static_cast<char32_t>(*code) : U'\0';
        const auto count = arity_of(c);
        if (!count || !has_arguments(*count)) {
            no_clause();
        }
        ++at_;
        spec.control = c;
        for (int i = 0; i < *count; ++i) {
            spec.args.push_back(argument());
        }
    }

    // The number of arguments control character `c` takes; none when it is not one.
    static std::optional<int> arity_of(char32_t c) {
        if (std::u32string_view(U"wpsefgbB+#ci").contains(c)) {
            return 1;
        }
        if (std::u32string_view(U"WPxX").contains(c)) {
            return 2;
        }
        return c == '~' || c == 'n' ? std::optional{0} : std::nullopt;
    }

    // Whether `count` arguments are left; for none, whether the arguments are a list.
    bool has_arguments(int count) const {
        if (count == 0) {
            return arguments_.is_list();
        }
        return arguments_.is_cons() && (count == 1 || arguments_.tail_unchecked().is_cons());
    }

    // collect_cseq/2: one control sequence after its '~'.
    Spec control() {
        Spec spec;
        spec.order = Terms(context_).atom("undefined");
        width(spec);
        precision_and_pad(spec);
        modifiers(spec);
        control_character(spec);
        return spec;
    }

    ProcessContext &context_;
    std::vector<Term> format_;
    // Next unread format element.
    std::size_t at_ = 0;
    // Arguments not yet consumed.
    Term arguments_;
};

// A spec map's value under `key`; none when the key is missing.
std::optional<Term> value_of(ProcessContext &context, const Term &map, std::string_view key) {
    return need(map.map_find(Terms(context).atom(key)));
}

// A spec map's value under a key it must have.
Term required(ProcessContext &context, const Term &map, std::string_view key) {
    const auto value = value_of(context, map, key);
    if (!value) {
        bad_argument();
    }
    return *value;
}

// none or a count.
std::optional<std::int64_t> count_of(const Term &term) {
    if (term.is_atom() && term.atom_spelling_unchecked() == "none") {
        return std::nullopt;
    }
    const auto value = string::small_of(term);
    if (!value || *value < -MAX_FIELD || *value > MAX_FIELD) {
        bad_argument();
    }
    return value;
}

// Whether `term` is the atom `name`.
bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

// A format_spec() map as a spec (io_lib:build_text/1's input).
Spec spec_of(ProcessContext &context, const Term &map) {
    Spec spec;
    const auto control = string::small_of(required(context, map, "control_char"));
    if (!control || *control < 0 || *control > 0x10FFFF) {
        bad_argument();
    }
    spec.control = static_cast<char32_t>(*control);
    Term args = required(context, map, "args");
    for (; args.is_cons(); args = args.tail_unchecked()) {
        spec.args.push_back(args.head_unchecked());
    }
    if (!args.is_nil()) {
        bad_argument();
    }
    spec.width = count_of(required(context, map, "width"));
    spec.precision = count_of(required(context, map, "precision"));
    const auto adjust = required(context, map, "adjust");
    const auto encoding = required(context, map, "encoding");
    if ((!is_atom(adjust, "left") && !is_atom(adjust, "right")) ||
        (!is_atom(encoding, "latin1") && !is_atom(encoding, "unicode"))) {
        bad_argument();
    }
    spec.left = is_atom(adjust, "left");
    spec.unicode = is_atom(encoding, "unicode");
    spec.pad = required(context, map, "pad_char");
    spec.strings = !is_atom(value_of(context, map, "strings").value_or(Terms(context).boolean(true)), "false");
    spec.order = value_of(context, map, "maps_order").value_or(Terms(context).atom("undefined"));
    return spec;
}

// none or the integer.
Term count_term(ProcessContext &context, const std::optional<std::int64_t> &value) {
    const Terms terms(context);
    return value ? terms.integer(*value) : terms.atom("none");
}

// unscan's print_field_width/2, print_precision/2 and print_pad_char/1 text.
// A spec map's value under a key print/1 matches on; no clause without it.
Term matched(ProcessContext &context, const Term &map, std::string_view key) {
    const auto value = value_of(context, map, key);
    if (!value) {
        no_clause();
    }
    return *value;
}

// The characters of an integer (negated when `negate`), appended.
void integer_chars(const Terms &terms, const Term &value, bool negate, std::vector<Term> &out) {
    if (!value.is_integer()) {
        bad_argument();
    }
    const auto number = need(detail::integer_read(value));
    for (const char c : integer_digits(negate ? detail::Integer{-number} : number, 10)) {
        out.push_back(terms.integer(c));
    }
}

// print_field_width/2 of a spec map.
void width_chars(ProcessContext &context, const Term &map, std::vector<Term> &out) {
    const Terms terms(context);
    const auto width = matched(context, map, "width");
    const auto adjust = matched(context, map, "adjust");
    if (is_atom(width, "none")) {
        return;
    }
    if (!is_atom(adjust, "left") && !is_atom(adjust, "right")) {
        no_clause();
    }
    integer_chars(terms, width, is_atom(adjust, "left"), out);
}

// print_precision/2 and print_pad_char/1 of a spec map: ".P", "." before a pad other than space, ".Pad".
void precision_chars(ProcessContext &context, const Term &map, std::vector<Term> &out) {
    const Terms terms(context);
    const auto precision = matched(context, map, "precision");
    const auto pad = matched(context, map, "pad_char");
    const bool space = string::is_char(pad, ' ');
    if (!is_atom(precision, "none")) {
        out.push_back(terms.integer('.'));
        integer_chars(terms, precision, false, out);
    } else if (!space) {
        out.push_back(terms.integer('.'));
    }
    if (!space) {
        out.push_back(terms.integer('.'));
        out.push_back(pad);
    }
}

// print_encoding/1 and print_strings/1: t for unicode, l without strings.
void modifier_chars(const Terms &terms, const Term &encoding, const Term &strings, std::vector<Term> &out) {
    if ((!is_atom(encoding, "latin1") && !is_atom(encoding, "unicode")) ||
        (!is_atom(strings, "true") && !is_atom(strings, "false"))) {
        no_clause();
    }
    if (is_atom(encoding, "unicode")) {
        out.push_back(terms.integer('t'));
    }
    if (is_atom(strings, "false")) {
        out.push_back(terms.integer('l'));
    }
}

// print_maps_order/1: k for ordered, K for reversed or a comparison fun.
void order_chars(const Terms &terms, const Term &order, std::vector<Term> &out) {
    if (is_atom(order, "ordered") || is_atom(order, "reversed") || order.is_function(2)) {
        out.push_back(terms.integer(is_atom(order, "ordered") ? 'k' : 'K'));
    } else if (!is_atom(order, "undefined")) {
        no_clause();
    }
}

// unscan's print/8 of one spec map: "~", fields, modifiers and the control character.
std::vector<Term> spec_text(ProcessContext &context, const Term &map) {
    const Terms terms(context);
    const auto control = matched(context, map, "control_char");
    std::vector<Term> out{terms.integer('~')};
    width_chars(context, map, out);
    precision_chars(context, map, out);
    modifier_chars(terms, matched(context, map, "encoding"), matched(context, map, "strings"), out);
    order_chars(terms, value_of(context, map, "maps_order").value_or(terms.atom("undefined")), out);
    out.push_back(control);
    return out;
}

// unscan's args/1 of one element: a spec's arguments, after its K ordering.
void spec_args(ProcessContext &context, const Term &element, std::vector<Term> &out) {
    if (!element.is_map()) {
        return;
    }
    const auto args = value_of(context, element, "args");
    if (!args) {
        return;
    }
    const auto order = value_of(context, element, "maps_order");
    if (order && (order->is_function(2) || is_atom(*order, "reversed"))) {
        out.push_back(*order);
    }
    Term rest = *args;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        out.push_back(rest.head_unchecked());
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
}
} // namespace

std::vector<Element> scan(ProcessContext &context, const Term &format, const Term &arguments) {
    return Collector(context, format_elements(context, format), arguments).run();
}

std::vector<Element> elements_of(ProcessContext &context, const Term &format_list) {
    std::vector<Element> elements;
    Term rest = format_list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto element = rest.head_unchecked();
        if (element.is_map()) {
            elements.emplace_back(spec_of(context, element));
        } else {
            elements.emplace_back(element);
        }
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    return elements;
}

Term spec_term(ProcessContext &context, const Spec &spec) {
    const Terms terms(context);
    const std::array<std::pair<Term, Term>, 9> entries{{
        {terms.atom("control_char"), terms.integer(spec.control)},
        {terms.atom("args"), terms.list(spec.args)},
        {terms.atom("width"), count_term(context, spec.width)},
        {terms.atom("adjust"), terms.atom(spec.left ? "left" : "right")},
        {terms.atom("precision"), count_term(context, spec.precision)},
        {terms.atom("pad_char"), spec.pad},
        {terms.atom("encoding"), terms.atom(spec.unicode ? "unicode" : "latin1")},
        {terms.atom("strings"), terms.boolean(spec.strings)},
        {terms.atom("maps_order"), spec.order},
    }};
    return need(TermFactory(context).map(entries));
}

Term unscan(ProcessContext &context, const Term &format_list) {
    const Terms terms(context);
    std::vector<Term> text;
    std::vector<Term> args;
    Term rest = format_list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto element = rest.head_unchecked();
        if (element.is_map()) {
            const auto part = spec_text(context, element);
            text.insert(text.end(), part.begin(), part.end());
        } else if (element.is_integer()) {
            text.push_back(element);
        } else {
            no_clause();
        }
    }
    if (!rest.is_nil()) {
        no_clause();
    }
    for (rest = format_list; rest.is_cons(); rest = rest.tail_unchecked()) {
        spec_args(context, rest.head_unchecked(), args);
    }
    return terms.tuple(std::array{terms.list(text), terms.list(args)});
}

Term option_of(const Term &options, std::string_view key, const Term &fallback) {
    Term rest = options;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto option = rest.head_unchecked();
        if (option.is_tuple() && option.tuple_size_unchecked() > 0 && is_atom(option.tuple_element_unchecked(0), key)) {
            return option.tuple_size_unchecked() == 2 ? option.tuple_element_unchecked(1) : fallback;
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return fallback;
}
} // namespace clause::runtime::builtins::io_lib
