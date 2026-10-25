#include "../io_lib/deep.hpp"
#include "../io_lib/format.hpp"
#include "../io_lib/write.hpp"
#include "front_end.hpp"
#include "pp.hpp"
#include <algorithm>
#include <array>

// The item arena of the C++ erl_pp (plan 21 step LIB34C) and the helpers both passes share.
namespace clause::runtime::builtins::front_end::pp {
using forms::element;
using forms::is_node;

Index Items::add(Item item) {
    items_.push_back(std::move(item));
    return Index{static_cast<std::uint32_t>(items_.size() - 1)};
}

Index Items::character(char32_t code) {
    Item item;
    item.kind = Kind::character;
    item.code = code;
    return add(std::move(item));
}

Index Items::leaf(std::u32string_view text) {
    Item item;
    item.kind = Kind::leaf;
    item.text = text;
    item.size = text_length(build_.context(), text);
    return add(std::move(item));
}

Index Items::leaf(std::string_view ascii) { return leaf(std::u32string(ascii.begin(), ascii.end())); }

Index Items::codes(std::u32string_view text) {
    Item item;
    item.kind = Kind::leaf;
    item.text = text;
    item.size = std::ssize(text);
    // A string list is a group of characters, not an atomic leaf.
    return concat({add(std::move(item))});
}

Index Items::word(std::string_view text) {
    Item item;
    item.kind = Kind::word;
    item.text.assign(text.begin(), text.end());
    item.size = std::ssize(text);
    return add(std::move(item));
}

Index Items::of_term(Kind kind, const Term &term) {
    Item item;
    item.kind = kind;
    item.term = term;
    return add(std::move(item));
}

Index Items::reserved(Index word) {
    Item item;
    item.kind = Kind::reserved;
    item.parts = {word};
    return add(std::move(item));
}

Index Items::concat(std::vector<Index> parts) {
    Item item;
    item.parts = std::move(parts);
    return add(std::move(item));
}

Index Items::empty() { return concat({}); }

Index Items::list(std::vector<Index> parts) {
    Item item;
    item.kind = Kind::list;
    item.parts = std::move(parts);
    item.spaced = true;
    return add(std::move(item));
}

Index Items::first(const FirstParts &shape) {
    Item item;
    item.kind = Kind::first;
    item.before = shape.before;
    item.parts = {shape.part};
    item.spaced = true;
    return add(std::move(item));
}

Index Items::step(Kind kind, Index head, Index tail) {
    Item item;
    item.kind = kind;
    item.parts = {head, tail};
    return add(std::move(item));
}

Index Items::seq(const SeqParts &shape, std::vector<Index> parts) {
    Item item;
    item.kind = Kind::seq;
    item.before = shape.before;
    item.after = shape.after;
    item.separator = shape.separator;
    item.last_separator = shape.last_separator;
    item.spaced = shape.spaced;
    item.parts = std::move(parts);
    return add(std::move(item));
}

Index Items::prefer_nl(Index separator, bool spaced, std::vector<Index> parts) {
    Item item;
    item.kind = Kind::prefer_nl;
    item.separator = separator;
    item.last_separator = separator;
    item.spaced = spaced;
    item.parts = std::move(parts);
    return add(std::move(item));
}

Index Items::force_nl(Index comment, Index part) {
    Item item;
    item.kind = Kind::force_nl;
    item.parts = {comment, part};
    return add(std::move(item));
}

Index Items::hook(const Term &expr, std::int64_t precedence) {
    Item item;
    item.kind = Kind::hook;
    item.term = expr;
    item.precedence = precedence;
    return add(std::move(item));
}

std::int64_t text_length(ProcessContext &context, std::u32string_view text) {
    // Below U+0300 only CR LF joins code points into one cluster.
    if (std::ranges::all_of(text, [](char32_t code) { return code < 0x300 && code != U'\r'; })) {
        return std::ssize(text);
    }
    const string::Terms terms(context);
    return string::small_of(string::length_of(context, terms.utf8(text))).value_or(0);
}

std::u32string chardata_text(ProcessContext & /*context*/, const Term &chars) {
    io_lib::Deep deep;
    return deep.flat(deep.term(chars));
}

std::u32string format_text(ProcessContext &context, std::string_view format, std::span<const Term> args) {
    const string::Terms terms(context);
    std::vector<Term> codes;
    codes.reserve(format.size());
    for (const auto code : format) {
        codes.push_back(terms.integer(static_cast<unsigned char>(code)));
    }
    io_lib::Deep deep;
    const auto elements = io_lib::scan(context, terms.list(codes), terms.list(args));
    return deep.flat(io_lib::build(context, deep, elements, terms.integer(-1)));
}

std::u32string write_text(ProcessContext &context, const Term &term) {
    io_lib::Deep deep;
    io_lib::Writer writer(context, deep, io_lib::Encoding::latin1, io_lib::MapsOrder::undefined);
    return deep.flat(writer.write(term, -1));
}

Index Printer::seq(const Brackets &brackets, Index separator, std::vector<Index> parts) {
    const auto listed_separator = listed(separator);
    return items_.seq({.before = brackets.open,
                       .after = brackets.close,
                       .separator = listed_separator,
                       .last_separator = listed_separator,
                       .spaced = true},
                      std::move(parts));
}

Index Printer::separated(Index separator, bool spaced, std::vector<Index> parts) {
    const auto none = items_.empty();
    return items_.seq(
        {.before = none, .after = none, .separator = separator, .last_separator = separator, .spaced = spaced},
        std::move(parts));
}

Index Printer::paren(std::int64_t precedence, std::int64_t outer, Index item) {
    return precedence < outer ? cat({ch('('), item, ch(')')}) : item;
}

std::vector<Index> Printer::items(const Term &exprs, const Opts &opts) {
    std::vector<Index> out;
    for (const auto &expr : forms::elements(exprs)) {
        out.push_back(item(expr, 0, opts));
    }
    return out;
}

Index Printer::arguments(const Term &args, const Opts &opts) {
    return seq({.open = ch('('), .close = ch(')')}, ch(','), items(args, opts));
}

Index Printer::tuple(std::vector<Index> parts) {
    return parts.empty() ? leaf("{}") : seq({.open = ch('{'), .close = ch('}')}, ch(','), std::move(parts));
}

// call/4: Name(Args), the name already printed at func_prec/0's left precedence.
Index Printer::call(Index name, const Term &args, std::int64_t precedence, const Opts &opts) {
    return paren(FUNC_PRECEDENCE, precedence, items_.first({.before = name, .part = arguments(args, opts)}));
}

Index Printer::text(std::string_view format, std::span<const Term> args) {
    return items_.leaf(format_text(build_.context(), format, args));
}

// abstract/2: erl_parse:abstract(Term, [{encoding, Encoding}]).
Term Printer::abstract(const Term &term, const Opts &opts) {
    const auto encoding = build_.atom(opts.unicode ? "unicode"_atom : "latin1"_atom);
    return abstract_term(build_.context(), term, build_.integer(0), encoding);
}

Index Printer::form(const Term &form, const Opts &opts) {
    if (is_node(form, "attribute", 4)) {
        return attribute(form, opts);
    }
    if (is_node(form, "function", 5)) {
        return function(form, opts);
    }
    if (is_node(form, "error", 2) || is_node(form, "warning", 2)) {
        return message(form, opts);
    }
    if (is_node(form, "eof", 2)) {
        return ch('\n');
    }
    lists::no_clause();
}

Index Printer::exprs(const Term &exprs, const Opts &opts) {
    const auto none = items_.empty();
    const auto comma = listed(ch(','));
    return items_.seq({.before = none, .after = none, .separator = comma, .last_separator = comma, .spaced = true},
                      items(exprs, opts));
}

Index Printer::item(const Term &node, std::int64_t precedence, const Opts &opts) {
    struct Clause {
        std::string_view tag;
        std::size_t size;
        Rule rule;
    };

    static constexpr auto CLAUSES = std::to_array<Clause>({
        {"var", 3, &Printer::variable},
        {"char", 3, &Printer::char_expr},
        {"integer", 3, &Printer::number},
        {"float", 3, &Printer::number},
        {"atom", 3, &Printer::atom_expr},
        {"string", 3, &Printer::string_expr},
        {"nil", 2, &Printer::nil},
        {"cons", 4, &Printer::cons},
        {"lc", 4, &Printer::comprehension},
        {"bc", 4, &Printer::comprehension},
        {"mc", 4, &Printer::comprehension},
        {"tuple", 3, &Printer::tuple_expr},
        {"record_index", 4, &Printer::record_index},
        {"record", 4, &Printer::record},
        {"record_field", 5, &Printer::record_field},
        {"record", 5, &Printer::record_update},
        {"record_field", 4, &Printer::field_access},
        {"map", 3, &Printer::map},
        {"map", 4, &Printer::map_update},
        {"block", 3, &Printer::block},
        {"if", 3, &Printer::if_expr},
        {"case", 4, &Printer::case_expr},
        {"cond", 3, &Printer::cond_expr},
        {"receive", 3, &Printer::receive},
        {"receive", 5, &Printer::receive_after},
        {"fun", 3, &Printer::fun3},
        {"fun", 4, &Printer::fun4},
        {"named_fun", 4, &Printer::named_fun},
        {"named_fun", 5, &Printer::named_fun_info},
        {"call", 4, &Printer::call_expr},
        {"try", 6, &Printer::try_expr},
        {"catch", 3, &Printer::catch_expr},
        {"maybe", 3, &Printer::maybe},
        {"maybe", 4, &Printer::maybe_else},
        {"maybe_match", 4, &Printer::maybe_match},
        {"match", 4, &Printer::match},
        {"op", 4, &Printer::prefix_op},
        {"op", 5, &Printer::infix_op},
        {"remote", 4, &Printer::remote},
        {"bin", 3, &Printer::binary},
        {"debug_line", 3, &Printer::debug_line},
        {"executable_line", 3, &Printer::debug_line},
        {"value", 3, &Printer::value},
    });
    const auto tag = forms::tag_of(node);
    const auto size = node.is_tuple() ? node.tuple_size_unchecked() : 0;
    for (const auto &clause : CLAUSES) {
        if (clause.tag == tag && clause.size == size) {
            return (this->*clause.rule)(node, precedence, opts);
        }
    }
    return unknown(node, precedence, opts);
}

// item/3's last clauses: INVALID-FORM without a hook, else the hook's text.
Index Printer::unknown(const Term &node, std::int64_t precedence, const Opts &opts) {
    if (!opts.hook) {
        const std::array args{node};
        return text("INVALID-FORM:~w:", args);
    }
    return items_.hook(node, precedence);
}
} // namespace clause::runtime::builtins::front_end::pp
