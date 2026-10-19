#include "../../terms/funs.hpp"
#include "../lists/list_walk.hpp"
#include "../string/string.hpp"
#include "abstract.hpp"
#include "parser.hpp"
#include <array>
#include <clause/runtime/code_server.hpp>
#include <terms.hpp>

// erl_parse's builtins (plan 21 step LIB31, docs/builtins.md#erl-parse): clause_erl_parse:parse/1 runs the grammar
// for library/stdlib/erl_parse.erl, clause_erl_parse:abstract/3 converts terms for the encodings that are atoms,
// and erl_parse's normalise/1, tokens/1,2, first_anno/1 and precedence tables.
namespace clause::runtime::builtins::front_end {
namespace {
using lists::no_clause;
using string::Terms;

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

Term element(const Term &tuple, std::size_t index) { return tuple.tuple_element_unchecked(index); }

bool is_node(const Term &term, std::string_view tag, std::size_t size) {
    return term.is_tuple() && term.tuple_size_unchecked() == size && is_atom(element(term, 0), tag);
}

Term pair(const Terms &terms, const Term &first, const Term &second) {
    const std::array items{first, second};
    return terms.tuple(items);
}

// The last token before the end the parser reached, for erl_parse.erl's yecctoken_end_location/1.
Term end_token(const Terms &terms, const Term &tokens) {
    std::optional<Term> last;
    for (auto rest = tokens; rest.is_cons(); rest = rest.tail_unchecked()) {
        last = rest.head_unchecked();
    }
    return last ? pair(terms, terms.atom("syntax_error_at_end"_atom), *last) : terms.atom("syntax_error_at_end"_atom);
}

// An ASCII format string as a character list.
Term chars_of(const Terms &terms, std::string_view text) {
    std::vector<Term> codes;
    for (const auto code : text) {
        codes.push_back(terms.integer(static_cast<unsigned char>(code)));
    }
    return terms.list(codes);
}

Term token_at(const Term &tokens, std::size_t index) {
    auto rest = tokens;
    for (; index > 0; --index) {
        rest = rest.tail_unchecked();
    }
    return rest.head_unchecked();
}

// parse(Tokens): {ok, Form}, {error, ErrorInfo}, {syntax_error, Token}, syntax_error_at_end or
// {syntax_error_at_end, LastToken}, or {format, Location, Format, Arguments} for erl_parse.erl to finish.
Term parse(ProcessContext &context, const Term &tokens) {
    const Terms terms(context);
    try {
        return pair(terms, terms.atom("ok"_atom), parsing::Parser(context, tokens).form());
    } catch (const parsing::SyntaxError &failure) {
        const auto count = need(tokens.is_proper_list()) ? need(tokens.list_length()) : SIZE_MAX;
        if (failure.index >= count) {
            return end_token(terms, tokens);
        }
        return pair(terms, terms.atom("syntax_error"_atom), token_at(tokens, failure.index));
    } catch (const parsing::ActionError &failure) {
        const std::array info{failure.location, terms.atom("erl_parse"_atom), failure.message};
        return pair(terms, terms.atom("error"_atom), terms.tuple(info));
    } catch (const parsing::FormatError &failure) {
        const std::array format{terms.atom("format"_atom), failure.location, chars_of(terms, failure.format),
                                terms.list(failure.arguments)};
        return terms.tuple(format);
    }
}

// The encodings abstract/3 takes as atoms: which integer lists are strings.
enum class Encoding : std::uint8_t { latin1, unicode, none };

Encoding encoding_of(const Term &encoding) {
    if (is_atom(encoding, "latin1")) {
        return Encoding::latin1;
    }
    if (is_atom(encoding, "unicode") || is_atom(encoding, "utf8")) {
        return Encoding::unicode;
    }
    if (is_atom(encoding, "none")) {
        return Encoding::none;
    }
    bad_argument();
}

// abstract/3 of OTP for the encodings latin1, unicode, utf8 and none.
class Abstract final {
  public:
    Abstract(ProcessContext &context, Encoding encoding, const Term &anno)
        : context_(context), terms_(context), anno_(anno), encoding_(encoding) {}

    Term value(const Term &term) const {
        if (const auto leaf = scalar(term)) {
            return *leaf;
        }
        if (term.is_bitstring()) {
            return bits(term);
        }
        if (term.is_cons()) {
            return encoding_ == Encoding::none ? conses(term) : list(term);
        }
        if (term.is_tuple()) {
            return node({terms_.atom("tuple"_atom), anno_, terms_.list(values(need(term.tuple_elements())))});
        }
        if (term.is_map()) {
            return map(term);
        }
        if (term.is_function()) {
            return fun(term);
        }
        no_clause();
    }

  private:
    // Numbers, atoms and [].
    std::optional<Term> scalar(const Term &term) const {
        if (term.is_integer() || term.is_float() || term.is_atom()) {
            const auto tag = term.is_integer() ? "integer" : term.is_float() ? "float" : "atom";
            return node({terms_.atom(tag), anno_, term});
        }
        if (term.is_nil()) {
            return node({terms_.atom("nil"_atom), anno_});
        }
        return std::nullopt;
    }

    Term node(std::initializer_list<Term> items) const {
        return terms_.tuple(std::span<const Term>(items.begin(), items.size()));
    }

    std::vector<Term> values(const std::vector<Term> &items) const {
        std::vector<Term> abstracted;
        abstracted.reserve(items.size());
        for (const auto &item : items) {
            abstracted.push_back(value(item));
        }
        return abstracted;
    }

    // The encoding's test of a character: latin1 below 256, unicode and utf8 any Unicode scalar.
    bool is_string_char(const Term &element) const {
        const auto code = string::small_of(element);
        if (!code || *code < 0) {
            return false;
        }
        if (encoding_ == Encoding::latin1) {
            return *code < 256;
        }
        return *code < 0xD800 || (*code > 0xDFFF && *code < 0xFFFE) || (*code > 0xFFFF && *code <= 0x10FFFF);
    }

    // [H | T] with the encoding none: conses all the way.
    Term conses(const Term &term) const {
        std::vector<Term> heads;
        auto rest = term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            heads.push_back(value(rest.head_unchecked()));
        }
        auto built = value(rest);
        for (auto head = heads.rbegin(); head != heads.rend(); ++head) {
            built = node({terms_.atom("cons"_atom), anno_, *head, built});
        }
        return built;
    }

    // abstract_list/4: a string while the elements are characters; the characters before another element become
    // {integer, A, C} conses, and the abstraction starts again after it.
    Term list(const Term &term) const {
        std::vector<Term> elements;
        auto rest = term;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            elements.push_back(rest.head_unchecked());
        }
        // Runs of characters, each ended by the element after it, the last one by the end of the list.
        std::vector<std::pair<std::size_t, std::size_t>> runs;
        for (std::size_t start = 0;;) {
            auto stop = start;
            while (stop < elements.size() && is_string_char(elements[stop])) {
                ++stop;
            }
            runs.emplace_back(start, stop);
            if (stop >= elements.size()) {
                break;
            }
            start = stop + 1;
        }
        auto built = last_run(std::span(elements).subspan(runs.back().first), rest);
        for (auto run = runs.rbegin() + 1; run != runs.rend(); ++run) {
            const auto chars = std::span(elements).subspan(run->first, run->second - run->first);
            built = wrap(chars, node({terms_.atom("cons"_atom), anno_, value(elements[run->second]), built}));
        }
        return built;
    }

    // The characters that end the list: [] or the tail alone, a string, or characters before an improper tail.
    Term last_run(std::span<const Term> chars, const Term &tail) const {
        if (chars.empty()) {
            return value(tail);
        }
        if (tail.is_nil()) {
            return node({terms_.atom("string"_atom), anno_, terms_.list(chars)});
        }
        return wrap(chars, value(tail));
    }

    // not_string/3: each character, last first, as a cons of {integer, A, C}.
    Term wrap(std::span<const Term> chars, Term built) const {
        for (auto code = chars.rbegin(); code != chars.rend(); ++code) {
            built = node({terms_.atom("cons"_atom), anno_, node({terms_.atom("integer"_atom), anno_, *code}), built});
        }
        return built;
    }

    // {bin, A, [{bin_element, A, {integer, A, Byte}, default, default}, ...]}, the last bits with their size.
    Term bits(const Term &term) const {
        const auto bytes = need(term.bitstring_bytes());
        const auto size = need(term.bit_size());
        std::vector<Term> elements;
        for (std::size_t index = 0; index < bytes.size(); ++index) {
            const auto bits_left = size - index * 8;
            const auto byte = std::to_integer<unsigned>(bytes[index]);
            const auto value = bits_left >= 8 ? byte : byte >> (8 - bits_left);
            const auto width =
                bits_left >= 8
                    ? terms_.atom("default"_atom)
                    : node({terms_.atom("integer"_atom), anno_, terms_.integer(static_cast<std::int64_t>(bits_left))});
            elements.push_back(node({terms_.atom("bin_element"_atom), anno_,
                                     node({terms_.atom("integer"_atom), anno_, terms_.integer(value)}), width,
                                     terms_.atom("default"_atom)}));
        }
        return node({terms_.atom("bin"_atom), anno_, terms_.list(elements)});
    }

    // {map, A, [{map_field_assoc, A, K, V}, ...]} in maps:to_list/1 order.
    Term map(const Term &term) const {
        std::vector<Term> fields;
        for (const auto &[key, item] : need(term.map_entries())) {
            fields.push_back(node({terms_.atom("map_field_assoc"_atom), anno_, value(key), value(item)}));
        }
        return node({terms_.atom("map"_atom), anno_, terms_.list(fields)});
    }

    // An external fun: {'fun', A, {function, M, F, Arity}}; a local fun is OTP's case_clause on {type, local}.
    Term fun(const Term &term) const {
        const auto &definition = *need(detail::fun_view(term)).definition;
        if (!definition.external) {
            const auto type = pair(terms_, terms_.atom("type"_atom), terms_.atom("local"_atom));
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::case_clause, .payload = type.word()};
        }
        const auto module = need(Term::from_word(definition.module, context_));
        const auto name = need(Term::from_word(definition.function, context_));
        const auto arity = terms_.integer(static_cast<std::int64_t>(definition.arity));
        return node({terms_.atom("fun"_atom), anno_,
                     node({terms_.atom("function"_atom), value(module), value(name), value(arity)})});
    }

    ProcessContext &context_;
    Terms terms_;
    Term anno_;
    Encoding encoding_;
};

Term abstract(ProcessContext &context, const Term &term, const Term &anno, const Term &encoding) {
    return Abstract(context, encoding_of(encoding), anno).value(term);
}

Term normalise(ProcessContext &context, const Term &node) { return abstract::normalise(context, node); }

Term first_anno(ProcessContext &context, const Term &node) { return abstract::first_anno(context, node); }

// tokens/2: the tokens of an abstract term, More after them.
class Tokens final {
  public:
    Tokens(ProcessContext &context, const Term &more) : terms_(context), more_(more) {}

    Term of(const Term &node) {
        emit(node);
        return terms_.list(tokens_, more_);
    }

  private:
    void symbol(AtomName name, const Term &anno) { tokens_.push_back(pair(terms_, terms_.atom(name), anno)); }

    void emit(const Term &node) {
        for (const auto tag : {"char", "integer", "float", "atom", "var", "string"}) {
            if (is_node(node, tag, 3)) {
                tokens_.push_back(node);
                return;
            }
        }
        if (is_node(node, "nil", 2)) {
            symbol("["_atom, element(node, 1));
            symbol("]"_atom, element(node, 1));
        } else if (is_node(node, "cons", 4)) {
            cons(node);
        } else if (is_node(node, "tuple", 3)) {
            symbol("{"_atom, element(node, 1));
            elements(node);
        } else if (is_node(node, "map", 3)) {
            symbol("#"_atom, element(node, 1));
            symbol("{"_atom, element(node, 1));
            elements(node);
        } else if (is_node(node, "map_field_assoc", 4)) {
            emit(element(node, 2));
            symbol("=>"_atom, element(node, 1));
            emit(element(node, 3));
        } else {
            no_clause();
        }
    }

    // '[' Head (',' Element)* then ']' or '|' Tail ']'.
    void cons(const Term &node) {
        symbol("["_atom, element(node, 1));
        emit(element(node, 2));
        auto rest = element(node, 3);
        for (; is_node(rest, "cons", 4); rest = element(rest, 3)) {
            symbol(","_atom, element(rest, 1));
            emit(element(rest, 2));
        }
        if (is_node(rest, "nil", 2)) {
            symbol("]"_atom, element(rest, 1));
            return;
        }
        if (!rest.is_tuple() || rest.tuple_size_unchecked() < 2) {
            bad_argument();
        }
        const auto anno = element(rest, 1);
        symbol("|"_atom, anno);
        emit(rest);
        symbol("]"_atom, anno);
    }

    // tokens_tuple/3: each comma and the closing brace take the annotation of the element before them.
    // The elements of a tuple or the fields of a map, after their opening brace.
    void elements(const Term &node) {
        auto previous = element(node, 1);
        auto first = true;
        auto rest = element(node, 2);
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto item = rest.head_unchecked();
            if (!first) {
                symbol(","_atom, previous);
            }
            emit(item);
            if (!item.is_tuple() || item.tuple_size_unchecked() < 2) {
                bad_argument();
            }
            previous = element(item, 1);
            first = false;
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        symbol("}"_atom, previous);
    }

    Terms terms_;
    // The tokens after the term's own.
    Term more_;
    std::vector<Term> tokens_;
};

Term tokens1(ProcessContext &context, const Term &node) { return Tokens(context, Terms(context).nil()).of(node); }

Term tokens2(ProcessContext &context, const Term &node, const Term &more) { return Tokens(context, more).of(node); }

// One row of a precedence table: an operator and its precedences.
struct Precedence {
    std::string_view name;
    std::array<std::int64_t, 3> values;
};

constexpr std::array<Precedence, 32> INOP_PRECEDENCES{{
    {"=", {150, 100, 100}},   {"!", {150, 100, 100}},    {"orelse", {160, 150, 150}}, {"andalso", {200, 160, 160}},
    {"==", {300, 200, 300}},  {"/=", {300, 200, 300}},   {"=<", {300, 200, 300}},     {"<", {300, 200, 300}},
    {">=", {300, 200, 300}},  {">", {300, 200, 300}},    {"=:=", {300, 200, 300}},    {"=/=", {300, 200, 300}},
    {"++", {400, 300, 300}},  {"--", {400, 300, 300}},   {"+", {400, 400, 500}},      {"-", {400, 400, 500}},
    {"bor", {400, 400, 500}}, {"bxor", {400, 400, 500}}, {"bsl", {400, 400, 500}},    {"bsr", {400, 400, 500}},
    {"or", {400, 400, 500}},  {"xor", {400, 400, 500}},  {"*", {500, 500, 600}},      {"/", {500, 500, 600}},
    {"div", {500, 500, 600}}, {"rem", {500, 500, 600}},  {"band", {500, 500, 600}},   {"and", {500, 500, 600}},
    {"#", {750, 700, 750}},   {"(", {750, 750, 800}},    {":", {900, 800, 900}},      {".", {900, 900, 1000}},
}};

constexpr std::array<Precedence, 6> PREOP_PRECEDENCES{{
    {"catch", {0, 100, 0}},
    {"+", {600, 700, 0}},
    {"-", {600, 700, 0}},
    {"bnot", {600, 700, 0}},
    {"not", {600, 700, 0}},
    {"#", {700, 800, 0}},
}};

constexpr std::array<Precedence, 16> TYPE_INOP_PRECEDENCES{{
    {"=", {150, 100, 100}},
    {"::", {150, 150, 160}},
    {"|", {180, 170, 170}},
    {"..", {300, 200, 300}},
    {"+", {400, 400, 500}},
    {"-", {400, 400, 500}},
    {"bor", {400, 400, 500}},
    {"bxor", {400, 400, 500}},
    {"bsl", {400, 400, 500}},
    {"bsr", {400, 400, 500}},
    {"*", {500, 500, 600}},
    {"/", {500, 500, 600}},
    {"div", {500, 500, 600}},
    {"rem", {500, 500, 600}},
    {"band", {500, 500, 600}},
    {"#", {800, 700, 800}},
}};

constexpr std::array<Precedence, 4> TYPE_PREOP_PRECEDENCES{{
    {"+", {600, 700, 0}},
    {"-", {600, 700, 0}},
    {"bnot", {600, 700, 0}},
    {"#", {700, 800, 0}},
}};

// The row of `name` as a tuple of its first `width` precedences; function_clause for any other operator.
template <std::size_t Size>
Term precedence_of(ProcessContext &context, const std::array<Precedence, Size> &table, const Term &name,
                   std::size_t width) {
    for (const auto &row : table) {
        if (is_atom(name, row.name)) {
            const Terms terms(context);
            std::vector<Term> items;
            items.reserve(width);
            for (std::size_t index = 0; index < width; ++index) {
                items.push_back(terms.integer(row.values[index]));
            }
            return terms.tuple(items);
        }
    }
    no_clause();
}

Term inop_prec(ProcessContext &context, const Term &name) { return precedence_of(context, INOP_PRECEDENCES, name, 3); }

Term preop_prec(ProcessContext &context, const Term &name) {
    return precedence_of(context, PREOP_PRECEDENCES, name, 2);
}

Term type_inop_prec(ProcessContext &context, const Term &name) {
    return precedence_of(context, TYPE_INOP_PRECEDENCES, name, 3);
}

Term type_preop_prec(ProcessContext &context, const Term &name) {
    return precedence_of(context, TYPE_PREOP_PRECEDENCES, name, 2);
}

Term func_prec(ProcessContext &context) {
    const Terms terms(context);
    return pair(terms, terms.integer(800), terms.integer(700));
}

Term max_prec(ProcessContext &context) { return Terms(context).integer(900); }

constexpr std::array ERL_PARSE_BUILTINS{
    typed_entry<parse>("clause_erl_parse", "parse"),
    typed_entry<abstract>("clause_erl_parse", "abstract"),
    typed_entry<first_anno>("erl_parse", "first_anno"),
    typed_entry<func_prec>("erl_parse", "func_prec"),
    typed_entry<inop_prec>("erl_parse", "inop_prec"),
    typed_entry<max_prec>("erl_parse", "max_prec"),
    typed_entry<normalise>("erl_parse", "normalise"),
    typed_entry<preop_prec>("erl_parse", "preop_prec"),
    typed_entry<tokens1>("erl_parse", "tokens"),
    typed_entry<tokens2>("erl_parse", "tokens"),
    typed_entry<type_inop_prec>("erl_parse", "type_inop_prec"),
    typed_entry<type_preop_prec>("erl_parse", "type_preop_prec"),
};
} // namespace
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> erl_parse_builtins() noexcept { return builtins::front_end::ERL_PARSE_BUILTINS; }
} // namespace clause::runtime
