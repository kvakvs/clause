#include "../../terms/floats.hpp"
#include "../../terms/structural_order.hpp"
#include "../collections/collections.hpp"
#include "../string/string.hpp"
#include "front_end.hpp"
#include <array>
#include <clause/abi/immediate_services.hpp>

// erl_anno (plan 21 step LIB8, docs/builtins.md#compiler-tables): annotations are a location (Line or
// {Line, Column}) or a non-empty list of {Item, Value} pairs; each function follows OTP's clauses, so odd lists get
// OTP's answers and errors (badarg from lists:keyfind/3, try_clause for a pair that is not {Item, Value},
// function_clause from lists:keydelete/3 and keyreplace/4, badarith in end_location/1).
namespace clause::runtime::builtins::front_end {
namespace {
using lists::no_clause;
using string::Terms;

// The roles of erl_anno's arguments (typed.hpp's Named).
struct AnnoRole;
struct ValueRole;
using Anno = Named<AnnoRole>;
// The new value of a set_* function.
using Value = Named<ValueRole>;

bool is_atom(const Term &term, std::string_view name) {
    return term.is_atom() && term.atom_spelling_unchecked() == name;
}

int sign_of(const Term &integer) {
    const auto order =
        need(detail::structural_order(integer, Term::from_word(*abi::v1::NativeIntegerEncoding::encode(0)).value()));
    return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

// OTP's ?LN(L): a non-negative integer.
bool is_line(const Term &term) { return term.is_integer() && sign_of(term) >= 0; }

// OTP's ?COL(C): a positive integer.
bool is_column(const Term &term) { return term.is_integer() && sign_of(term) > 0; }

// A two-element tuple's elements; none for anything else.
std::optional<std::pair<Term, Term>> pair_of(const Term &term) { return collections::pair_of(term); }

// {Line, Column} with a valid line and column.
bool is_line_column(const Term &term) {
    const auto pair = pair_of(term);
    return pair && is_line(pair->first) && is_column(pair->second);
}

// A location: a line or a {Line, Column}.
bool is_location(const Term &term) { return is_line(term) || is_line_column(term); }

// lists:keyfind(Item, 1, Anno): the first tuple whose first element is Item; badarg for a list it cannot walk.
std::optional<Term> keyfind(const Term &anno, std::string_view item) {
    auto rest = anno;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto element = rest.head_unchecked();
        const auto first = collections::first_element(element);
        if (first && is_atom(*first, item)) {
            return element;
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return std::nullopt;
}

// anno_info/2,3: the Value of {Item, Value}, none when absent; any other tuple found is OTP's try_clause.
std::optional<Term> anno_info(const Term &anno, std::string_view item) {
    const auto found = keyfind(anno, item);
    if (!found) {
        return std::nullopt;
    }
    const auto pair = pair_of(*found);
    if (!pair) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::try_clause, .payload = found->word()};
    }
    return pair->second;
}

// What erl_anno answers: annotation values and the atoms it returns.
class Annotations final {
  public:
    explicit Annotations(ProcessContext &context) : terms_(context) {}

    Term atom(const AtomName name) const { return terms_.atom(name); }

    Term boolean(bool value) const { return terms_.boolean(value); }

    // An item's value or the atom `fallback`.
    Term info(const Term &anno, std::string_view item, AtomName fallback) const {
        return anno_info(anno, item).value_or(atom(fallback));
    }

    // location/1: the location itself, or the location item (0 when absent).
    Term location(const Term &anno) const {
        if (is_location(anno)) {
            return anno;
        }
        return anno_info(anno, "location").value_or(terms_.integer(0));
    }

    // end_location/3 and /2: walk the text from the location, counting lines (and columns when there is one).
    Term walk_text(const Anno &anno, const Term &text) const {
        const auto from = location(anno.term);
        const auto start = pair_of(from);
        TextEnd end{.line = start ? start->first : from, .column = start ? std::optional{start->second} : std::nullopt};
        auto rest = text;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            step(end, rest.head_unchecked());
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        return end.column ? tuple(end.line, *end.column) : end.line;
    }

    Term tuple(const Term &first, const Term &second) const {
        const std::array items{first, second};
        return terms_.tuple(items);
    }

    // [{Item, Value} | Anno].
    Term with(AtomName item, const Term &value, const Term &anno) const {
        return terms_.cons(tuple(atom(item), value), anno);
    }

    // simplify/1: [{location, L}] is L.
    static Term simplify(const Term &anno) {
        if (anno.is_cons() && anno.tail_unchecked().is_nil()) {
            const auto pair = pair_of(anno.head_unchecked());
            if (pair && is_atom(pair->first, "location")) {
                return pair->second;
            }
        }
        return anno;
    }

    // lists:keydelete(Item, 1, Anno): without the first tuple whose first element is Item.
    Term keydelete(const Term &anno, std::string_view item) const {
        std::vector<Term> before;
        auto rest = anno;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto first = collections::first_element(rest.head_unchecked());
            if (first && is_atom(*first, item)) {
                return terms_.list(before, rest.tail_unchecked());
            }
            before.push_back(rest.head_unchecked());
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        return terms_.list(before);
    }

    // lists:keyreplace(Item, 1, Anno, New): the first tuple whose first element is Item replaced.
    Term keyreplace(const Term &anno, std::string_view item, const Term &replacement) const {
        std::vector<Term> before;
        auto rest = anno;
        for (; rest.is_cons(); rest = rest.tail_unchecked()) {
            const auto first = collections::first_element(rest.head_unchecked());
            if (first && is_atom(*first, item)) {
                return terms_.list(before, terms_.cons(replacement, rest.tail_unchecked()));
            }
            before.push_back(rest.head_unchecked());
        }
        if (!rest.is_nil()) {
            no_clause();
        }
        return terms_.list(before);
    }

    // set/3: a location becomes [{location, L}] first; [] and other non-lists are badarg.
    Term set(std::string_view item, const Term &value, const Term &anno) const {
        if (!settable(item, value)) {
            bad_argument();
        }
        if (is_location(anno)) {
            return set_anno(item, value, terms_.list(std::array{tuple(atom("location"_atom), anno)}));
        }
        if (!anno.is_cons()) {
            bad_argument();
        }
        return set_anno(item, value, anno);
    }

  private:
    // Where end_location/1's walk has reached: a line, and a column when the location has one.
    struct TextEnd {
        Term line;
        std::optional<Term> column;
    };

    // One character of the text: a newline starts the next line at column 1, others move the column.
    void step(TextEnd &end, const Term &code) const {
        if (string::is_char(code, '\n')) {
            end.line = collections::add(terms_.context(), end.line, terms_.integer(1));
            end.column = end.column ? std::optional{terms_.integer(1)} : std::nullopt;
        } else if (end.column) {
            end.column = collections::add(terms_.context(), *end.column, terms_.integer(1));
        }
    }

    // set_anno/3: a default value (generated or record false) removes the item; others are added or replaced.
    Term set_anno(std::string_view item, const Term &value, const Term &anno) const {
        if ((item == "generated" || item == "record") && is_atom(value, "false")) {
            return simplify(keydelete(anno, item));
        }
        const auto current = anno_info(anno, item);
        if (!current || is_atom(*current, "undefined")) {
            return simplify(with(item, value, anno));
        }
        return simplify(keyreplace(anno, item, tuple(atom(item), value)));
    }

    // is_settable/2.
    static bool settable(std::string_view item, const Term &value) {
        if (item == "file") {
            return value.is_list() || value.is_binary();
        }
        if (item == "generated" || item == "record") {
            return is_atom(value, "true") || is_atom(value, "false");
        }
        if (item == "location" || item == "end_location") {
            return is_location(value);
        }
        return item == "text" && value.is_list();
    }

    Terms terms_;
};

// is_anno2/2: whether one {Item, Value} pair is a valid annotation.
bool valid_item(const Term &item, const Term &value) {
    if (is_atom(item, "location") || is_atom(item, "end_location")) {
        return is_location(value);
    }
    if (is_atom(item, "generated") || is_atom(item, "record")) {
        return is_atom(value, "true");
    }
    if (is_atom(item, "file")) {
        return value.is_list() || value.is_binary();
    }
    return is_atom(item, "text") && value.is_list();
}

// is_anno(Term): a location, or a proper non-empty list of valid pairs including a location.
Term is_anno(ProcessContext &context, const Anno &anno) {
    const Annotations answer(context);
    if (is_location(anno.term)) {
        return answer.boolean(true);
    }
    bool located = false;
    auto rest = anno.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        const auto pair = pair_of(rest.head_unchecked());
        if (!pair || !valid_item(pair->first, pair->second)) {
            return answer.boolean(false);
        }
        located = located || is_atom(pair->first, "location");
    }
    return answer.boolean(rest.is_nil() && located);
}

// new(Location): the location itself; badarg for anything else.
Term new1(ProcessContext & /*context*/, const Term &location) {
    if (!is_location(location)) {
        bad_argument();
    }
    return location;
}

Term to_term(ProcessContext & /*context*/, const Anno &anno) { return anno.term; }

// from_term(Term): a negative line (before OTP 19) is a generated annotation.
Term from_term(ProcessContext &context, const Anno &anno) {
    if (anno.term.is_integer() && sign_of(anno.term) < 0) {
        const Annotations answer(context);
        const auto line = need(
            detail::numeric_service(context, abi::v1::ImmediateOperation::subtract,
                                    Term::from_word(*abi::v1::NativeIntegerEncoding::encode(0)).value(), anno.term));
        return answer.set("generated", answer.atom("true"_atom), line);
    }
    return anno.term;
}

Term location(ProcessContext &context, const Anno &anno) { return Annotations(context).location(anno.term); }

// column(Anno): the column of a {Line, Column} location, else undefined.
Term column(ProcessContext &context, const Anno &anno) {
    const Annotations answer(context);
    const auto pair = pair_of(answer.location(anno.term));
    return pair ? pair->second : answer.atom("undefined"_atom);
}

// line(Anno): the line of the location.
Term line(ProcessContext &context, const Anno &anno) {
    const auto location = Annotations(context).location(anno.term);
    const auto pair = pair_of(location);
    return pair ? pair->first : location;
}

// An item of a list annotation; a location answers `fallback` (undefined or false) at once.
template <std::size_t Item> Term item_of(ProcessContext &context, const Anno &anno) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 4> ITEMS{
        {{"file", "undefined"}, {"generated", "false"}, {"record", "false"}, {"text", "undefined"}}};
    const auto [item, fallback] = std::get<Item>(ITEMS);
    const Annotations answer(context);
    if (is_location(anno.term)) {
        return answer.atom(fallback);
    }
    return answer.info(anno.term, item, fallback);
}

// end_location(Anno): the end_location item, or the end of the text item from the location.
Term end_location(ProcessContext &context, const Anno &anno) {
    const Annotations answer(context);
    if (is_location(anno.term)) {
        return answer.atom("undefined"_atom);
    }
    if (const auto end = anno_info(anno.term, "end_location"); end && !is_atom(*end, "undefined")) {
        return *end;
    }
    const auto text = anno_info(anno.term, "text");
    if (!text || is_atom(*text, "undefined")) {
        return answer.atom("undefined"_atom);
    }
    return answer.walk_text(anno, *text);
}

// set_file/2, set_generated/2, set_record/2, set_text/2 and set_end_location/2.
template <std::size_t Item> Term set_item(ProcessContext &context, const Value &value, const Anno &anno) {
    static constexpr std::array<std::string_view, 5> ITEMS{"file", "generated", "record", "text", "end_location"};
    return Annotations(context).set(std::get<Item>(ITEMS), value.term, anno.term);
}

// set_location(Location, Anno): a plain location stays plain; a list annotation gets its location item set.
Term set_location(ProcessContext &context, const Value &location, const Anno &anno) {
    if (is_location(anno.term) && is_location(location.term)) {
        return location.term;
    }
    return Annotations(context).set("location", location.term, anno.term);
}

// set_line(Line, Anno): the location's line replaced, its column kept.
Term set_line(ProcessContext &context, const Value &line, const Anno &anno) {
    const Annotations answer(context);
    const auto pair = pair_of(answer.location(anno.term));
    const auto location = pair ? answer.tuple(line.term, pair->second) : line.term;
    return set_location(context, {location}, anno);
}

constexpr std::array ERL_ANNO_BUILTINS{
    typed_entry<column>("erl_anno", "column"),
    typed_entry<end_location>("erl_anno", "end_location"),
    typed_entry<item_of<0>>("erl_anno", "file"),
    typed_entry<from_term>("erl_anno", "from_term"),
    typed_entry<item_of<1>>("erl_anno", "generated"),
    typed_entry<is_anno>("erl_anno", "is_anno"),
    typed_entry<line>("erl_anno", "line"),
    typed_entry<location>("erl_anno", "location"),
    typed_entry<new1>("erl_anno", "new"),
    typed_entry<item_of<2>>("erl_anno", "record"),
    typed_entry<set_item<4>>("erl_anno", "set_end_location"),
    typed_entry<set_item<0>>("erl_anno", "set_file"),
    typed_entry<set_item<1>>("erl_anno", "set_generated"),
    typed_entry<set_line>("erl_anno", "set_line"),
    typed_entry<set_location>("erl_anno", "set_location"),
    typed_entry<set_item<2>>("erl_anno", "set_record"),
    typed_entry<set_item<3>>("erl_anno", "set_text"),
    typed_entry<item_of<3>>("erl_anno", "text"),
    typed_entry<to_term>("erl_anno", "to_term"),
};
} // namespace

Term anno_location(ProcessContext &context, const Term &anno) { return location(context, {anno}); }

Term anno_end_location(ProcessContext &context, const Term &anno) { return end_location(context, {anno}); }

Term anno_set(ProcessContext &context, std::string_view item, const Term &value, const Term &anno) {
    return Annotations(context).set(item, value, anno);
}

Term anno_set_location(ProcessContext &context, const Term &location, const Term &anno) {
    return set_location(context, {location}, {anno});
}
} // namespace clause::runtime::builtins::front_end

namespace clause::runtime {
std::span<const BuiltinEntry> erl_anno_builtins() noexcept { return builtins::front_end::ERL_ANNO_BUILTINS; }
} // namespace clause::runtime
