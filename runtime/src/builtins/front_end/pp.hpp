#pragma once
#include "forms.hpp"
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

// erl_pp in C++ (plan 21 step LIB34C, docs/builtins.md#erl-pp): the abstract format printed as Erlang source in two
// passes, as Clause's erl_pp.erl did them. The first pass builds layout items in an arena; the second lays them out:
// a group goes on one line when it fits the line width, else its parts go on lines of their own, indented. Hook items
// (forms erl_pp does not know) take their text from answers erl_pp.erl collects by calling the hook.
namespace clause::runtime::builtins::front_end::pp {
// What one item is (erl_pp.erl's item shapes).
enum class Kind : std::uint8_t {
    // A code point, a leaf of known size, and a keyword or operator laid as its text.
    character,
    leaf,
    word,
    // Atoms, singleton atom types, chars, strings and values, written when laid out.
    atom,
    singleton,
    char_value,
    string,
    value,
    // A word or leaf that stays a part of its own in a group.
    reserved,
    // Items one after the other ([I | Is]).
    concat,
    list,
    first,
    seq,
    prefer_nl,
    force_nl,
    step,
    cstep,
    hook,
};

// An item's place in the arena (a distinct type, so it never passes as a column or count).
enum class Index : std::uint32_t {};

// One item: its kind and the fields its kind uses.
struct Item {
    Kind kind = Kind::concat;
    // A character's code point.
    char32_t code = 0;
    // A leaf's or word's text and size.
    std::u32string text;
    std::int64_t size = 0;
    // The term of an atom, char, string, value or hook expression; a hook's precedence.
    Term term;
    std::int64_t precedence = 0;
    // The children: concat, list, seq and prefer_nl parts; step and cstep head and tail; force_nl comment and item;
    // first's part.
    std::vector<Index> parts;
    // seq: before, after, separator and last separator items, and whether parts on one line are spaced.
    std::optional<Index> before;
    std::optional<Index> after;
    std::optional<Index> separator;
    std::optional<Index> last_separator;
    bool spaced = false;
};

// The size of laid characters: a width on one line, or the width of the last line once they span lines.
struct Size {
    std::int64_t width = 0;
    bool line = false;

    bool empty() const { return width == 0 && !line; }
};

// Laid characters and their size.
struct Laid {
    std::u32string chars;
    Size size;
};

// How leaves are written and laid out (pp/1).
struct Writers {
    bool unicode = false;
    bool quote_singleton = false;
    std::int64_t width = 72;
    std::int64_t indent = 4;
};

// The layout needs the next hook's text: the expression, its column and precedence.
struct HookNeeded {
    Term expr;
    std::int64_t column = 0;
    std::int64_t precedence = 0;
};

// The item arena of one print and its builders, one per item shape of erl_pp.erl.
class Items final {
  public:
    explicit Items(ProcessContext &context) : build_(context) {}

    forms::Builder build_;
    // A deque, so items stay where they are while the layout adds string pieces.
    std::deque<Item> items_;

    // The parts of {seq, Before, After, Sep, Parts}: Sep [S] or [{S, Last}] spaces parts on one line, a bare S
    // does not.
    struct SeqParts {
        Index before;
        Index after;
        Index separator;
        Index last_separator;
        bool spaced;
    };

    Index add(Item item);
    // A code point (an integer item).
    Index character(char32_t code);
    // leaf/1: its size the grapheme count.
    Index leaf(std::u32string_view text);
    Index leaf(std::string_view ascii);
    // A string of code points ("fun ", atom_to_list/1): one column each.
    Index codes(std::u32string_view text);
    // A keyword or operator atom.
    Index word(std::string_view text);
    // {atom, A}, {singleton_atom_type, A}, {char, C}, {string, S} and {value, V}.
    Index of_term(Kind kind, const Term &term);
    Index reserved(Index word);
    Index concat(std::vector<Index> parts);
    Index empty();
    Index list(std::vector<Index> parts);

    // {first, Before, Part}.
    struct FirstParts {
        Index before;
        Index part;
    };

    Index first(const FirstParts &shape);
    Index step(Kind kind, Index head, Index tail);
    Index seq(const SeqParts &shape, std::vector<Index> parts);
    Index prefer_nl(Index separator, bool spaced, std::vector<Index> parts);
    Index force_nl(Index comment, Index item);
    Index hook(const Term &expr, std::int64_t precedence);
};

// Grapheme count of a text (string:length/1).
std::int64_t text_length(ProcessContext &context, std::u32string_view text);
// The code points of chardata.
std::u32string chardata_text(ProcessContext &context, const Term &chars);
// io_lib:format(Format, Args) flattened.
std::u32string format_text(ProcessContext &context, std::string_view format, std::span<const Term> args);
// io_lib:write/1 flattened.
std::u32string write_text(ProcessContext &context, const Term &term);

// The options of the first pass (opts/1): whether a hook prints the forms erl_pp does not know, and the encoding of
// messages and abstract terms.
struct Opts {
    bool hook = false;
    bool unicode = true;
};

// opts(none).
inline constexpr Opts NO_HOOK{};

// The first pass (erl_pp.erl's form_items/2, item/3 and their helpers): abstract forms as items.
class Printer final {
  public:
    explicit Printer(Items &items) : items_(items), build_(items.build_) {}

    Index form(const Term &form, const Opts &opts);
    Index attribute(const Term &attribute, const Opts &opts);
    Index function(const Term &function, const Opts &opts);
    Index guard(const Term &guard, const Opts &opts);
    // exprs/3: {seq, [], [], [$,], Items}.
    Index exprs(const Term &exprs, const Opts &opts);
    Index item(const Term &node, std::int64_t precedence, const Opts &opts);

  private:
    // An item/3 or type_item/2 clause.
    using Rule = Index (Printer::*)(const Term &node, std::int64_t precedence, const Opts &opts);
    // A clause printer of clauses/3.
    using ClauseRule = Index (Printer::*)(const Term &clause, const Opts &opts);

    // Builders of the shapes erl_pp.erl writes inline (pp_items.cpp).
    Index ch(char32_t code) { return items_.character(code); }

    Index leaf(std::string_view ascii) { return items_.leaf(ascii); }

    Index word(std::string_view text) { return items_.word(text); }

    Index cat(std::vector<Index> parts) { return items_.concat(std::move(parts)); }

    Index atom(const Term &name) { return items_.of_term(Kind::atom, name); }

    Index reserved(std::string_view text) { return items_.reserved(word(text)); }

    // [S] as a separator.
    Index listed(Index separator) { return cat({separator}); }

    // The items before and after a sequence's parts.
    struct Brackets {
        Index open;
        Index close;
    };

    // {seq, Before, After, [Sep], Parts}.
    Index seq(const Brackets &brackets, Index separator, std::vector<Index> parts);
    // {seq, [], [], Sep, Parts} with a bare (unspaced) or listed (spaced) separator.
    Index separated(Index separator, bool spaced, std::vector<Index> parts);
    Index paren(std::int64_t precedence, std::int64_t outer, Index item);
    std::vector<Index> items(const Term &exprs, const Opts &opts);
    Index arguments(const Term &args, const Opts &opts);
    Index tuple(std::vector<Index> parts);
    Index call(Index name, const Term &args, std::int64_t precedence, const Opts &opts);
    Index text(std::string_view format, std::span<const Term> args);
    Term abstract(const Term &term, const Opts &opts);

    // Forms, attributes and clauses (pp_forms.cpp).
    Index message(const Term &message, const Opts &opts);
    Index attribute_body(const Term &attribute, const Opts &opts);
    // An attribute_body/3 clause; none when its argument does not match.
    using AttributeRule = std::optional<Index> (Printer::*)(const Term &attribute, const Opts &opts);
    Term dotted_var(const Term &name);
    std::optional<Index> module_attribute(const Term &attribute, const Opts &opts);
    std::optional<Index> names_attribute(const Term &attribute, const Opts &opts);
    std::optional<Index> import_attribute(const Term &attribute, const Opts &opts);
    std::optional<Index> optional_callbacks(const Term &attribute, const Opts &opts);
    std::optional<Index> file_attribute(const Term &attribute, const Opts &opts);
    std::optional<Index> record_attribute(const Term &attribute, const Opts &opts);
    Index simple_attribute(const Term &name, std::span<const Term> args);
    Index listed_attribute(const Term &name, std::vector<Index> args);
    std::u32string dotted(const Term &name);
    std::vector<Index> name_arities(const Term &pairs);
    Index function_clause(const Term &name, const Term &clause, const Opts &opts);
    Index guarded(Index before, const Term &guard, const Opts &opts);
    Index guard_items(const Term &guard, const Opts &opts);
    Index conjunction(const Term &tests, const Opts &opts);
    Index body(const Term &exprs, const Opts &opts);
    Index clauses(ClauseRule rule, const Term &clauses, const Opts &opts);
    Index if_clause(const Term &clause, const Opts &opts);
    Index if_guard(const Term &guard, const Opts &opts);
    Index case_clause(const Term &clause, const Opts &opts);
    Index catch_clause(const Term &clause, const Opts &opts);
    Index cond_clause(const Term &clause, const Opts &opts);

    // A fun's clauses and its name, [] for an unnamed fun.
    struct FunClauses {
        Term clauses;
        Term name;
    };

    Index fun_clauses(const FunClauses &fun, const Opts &opts);
    Index qualifiers(const Term &qualifiers, const Opts &opts);
    Index qualifier(const Term &qualifier, const Opts &opts);

    // Types and specifications (pp_types.cpp).
    Index type_attribute(const Term &attribute);
    Index spec_attribute(const Term &attribute);
    Index type_item(const Term &type, std::int64_t precedence);
    Index ann_type(const Term &node, std::int64_t precedence);
    // A type_item/2 clause of a {type, _, Name, Args} type; none when its arguments do not match.
    using TypeRule = std::optional<Index> (Printer::*)(const Term &node, std::int64_t precedence);
    Index builtin_type(const Term &node, std::int64_t precedence);
    std::optional<Index> union_type(const Term &node, std::int64_t precedence);
    std::optional<Index> list_type(const Term &node, std::int64_t precedence);
    std::optional<Index> nonempty_list_type(const Term &node, std::int64_t precedence);
    std::optional<Index> nil_type(const Term &node, std::int64_t precedence);
    std::optional<Index> map_type(const Term &node, std::int64_t precedence);
    std::optional<Index> tuple_type(const Term &node, std::int64_t precedence);
    std::optional<Index> record_type(const Term &node, std::int64_t precedence);
    std::optional<Index> range_type(const Term &node, std::int64_t precedence);
    std::optional<Index> binary_type_item(const Term &node, std::int64_t precedence);
    std::optional<Index> fun_type_item(const Term &node, std::int64_t precedence);
    Index binary_type(const Term &base, const Term &unit);
    Index map_pair_type(const Term &pair, std::int64_t precedence);
    Index field_type(const Term &field);
    Index typed(Index before, const Term &type);
    Index signature(const Term &type);
    Index constraint(const Term &constraint);
    Index fun_type(std::optional<Index> before, const Term &fun);
    Index named_type(Index name, const Term &types);
    Index type_list(const Term &types);

    // Expressions (pp_exprs.cpp).
    Index variable(const Term &node, std::int64_t precedence, const Opts &opts);
    Index char_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index number(const Term &node, std::int64_t precedence, const Opts &opts);
    Index atom_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index string_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index nil(const Term &node, std::int64_t precedence, const Opts &opts);
    Index cons(const Term &node, std::int64_t precedence, const Opts &opts);
    Index comprehension(const Term &node, std::int64_t precedence, const Opts &opts);
    Index tuple_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index record_index(const Term &node, std::int64_t precedence, const Opts &opts);
    Index record(const Term &node, std::int64_t precedence, const Opts &opts);
    Index record_field(const Term &node, std::int64_t precedence, const Opts &opts);
    Index record_update(const Term &node, std::int64_t precedence, const Opts &opts);
    Index field_access(const Term &node, std::int64_t precedence, const Opts &opts);
    Index map(const Term &node, std::int64_t precedence, const Opts &opts);
    Index map_update(const Term &node, std::int64_t precedence, const Opts &opts);
    Index block(const Term &node, std::int64_t precedence, const Opts &opts);
    Index if_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index case_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index cond_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index receive(const Term &node, std::int64_t precedence, const Opts &opts);
    Index receive_after(const Term &node, std::int64_t precedence, const Opts &opts);
    Index fun3(const Term &node, std::int64_t precedence, const Opts &opts);
    Index fun4(const Term &node, std::int64_t precedence, const Opts &opts);
    Index named_fun(const Term &node, std::int64_t precedence, const Opts &opts);
    Index named_fun_info(const Term &node, std::int64_t precedence, const Opts &opts);
    Index call_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index try_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index catch_expr(const Term &node, std::int64_t precedence, const Opts &opts);
    Index maybe(const Term &node, std::int64_t precedence, const Opts &opts);
    Index maybe_else(const Term &node, std::int64_t precedence, const Opts &opts);
    Index maybe_match(const Term &node, std::int64_t precedence, const Opts &opts);
    Index match(const Term &node, std::int64_t precedence, const Opts &opts);
    Index prefix_op(const Term &node, std::int64_t precedence, const Opts &opts);
    Index infix_op(const Term &node, std::int64_t precedence, const Opts &opts);
    Index remote(const Term &node, std::int64_t precedence, const Opts &opts);
    Index binary(const Term &node, std::int64_t precedence, const Opts &opts);
    Index debug_line(const Term &node, std::int64_t precedence, const Opts &opts);
    Index value(const Term &node, std::int64_t precedence, const Opts &opts);
    Index unknown(const Term &node, std::int64_t precedence, const Opts &opts);
    std::optional<Index> fun_function(const Term &function, const Opts &opts);
    Index fun_items(const FunClauses &fun, const Opts &opts);
    Index local_fun(const Term &function);
    Index fun_info(const Term &extra);
    Index list_items(const Term &node, const Opts &opts);
    Index bin_element(const Term &element, const Opts &opts);
    Index bin_type(const Term &type);
    std::vector<Index> record_name(const Term &name);
    Index record_fields(const Term &fields, const Opts &opts);
    Index record_field_def(const Term &field, const Opts &opts);
    Index map_field(const Term &field, const Opts &opts);
    Index map_fields(const Term &fields, const Opts &opts);
    Index remote_items(const Term &module, const Term &function, std::int64_t precedence, const Opts &opts);

    Items &items_;
    const forms::Builder &build_;
};

// The second pass over an item with the hook answers given so far; throws HookNeeded for the next one.
std::u32string layout(Items &items, Index item, std::int64_t indent, const Writers &writers,
                      std::span<const Term> answers);
} // namespace clause::runtime::builtins::front_end::pp
