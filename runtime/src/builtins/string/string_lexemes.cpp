#include "../lists/list_walk.hpp"
#include "search.hpp"
#include <array>

// string's lexemes, nth_lexeme, split, replace and find (plan 21 step LIB4): string.erl's lexemes_m, lexeme_pick,
// lexeme_skip, nth_lexeme_m, split_1, find_l and find_r, clause for clause, over the search machinery.
namespace clause::runtime::builtins::string {
namespace {
using lists::no_clause;

std::size_t byte_size(const Term &binary) {
    if (!binary.is_binary()) {
        bad_argument();
    }
    return binary_bytes(binary).size();
}

bool empty_binary(const Term &term) { return term.is_binary() && byte_size(term) == 0; }

// gc(Data) as a cons; badmatch for another answer.
unicode::Next cluster_of(ProcessContext &context, const Term &data) {
    const auto next = gc_term(context, data);
    if (!next.is_cons()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = next.word()};
    }
    return split(next);
}

// cp(Data) as a cons, none at [], case_clause for an error.
std::optional<unicode::Next> point_of(ProcessContext &context, const Term &data) {
    const auto next = cp_term(context, data);
    if (next.is_nil()) {
        return std::nullopt;
    }
    if (!next.is_cons()) {
        case_clause(next);
    }
    return split(next);
}

// A token being collected: OTP's reversed Tkn.
class Token final {
  public:
    explicit Token(const Terms &terms) : terms_(terms), acc_(terms.nil()) {}

    void point(const Term &cp) { acc_ = terms_.cons(cp, acc_); }

    void cluster(const Term &gc) { acc_ = terms_.append(terms_.rev(gc), acc_); }

    void binary(const Term &bin) { acc_ = terms_.cons(bin, acc_); }

    Term rev() const { return terms_.rev(acc_); }

    Term btoken(const Term &part) const { return terms_.btoken(part, {acc_}); }

  private:
    const Terms &terms_;
    Term acc_;
};

struct Picked {
    Term lexeme;
    Term rest;
};

// A code point at the start inside lexeme_pick: a separator's cluster ends the lexeme.
std::optional<Picked> pick_point(ProcessContext &context, Term &cs0, const unicode::Next &next, const Seps &seps,
                                 Token &token) {
    if (!member(seps.points, next.item)) {
        token.point(next.item);
        cs0 = next.rest;
        return std::nullopt;
    }
    const auto [gc, cs2] = cluster_of(context, cs0);
    if (member(seps.clusters, gc)) {
        return Picked{.lexeme = token.rev(), .rest = cs2};
    }
    token.cluster(gc);
    cs0 = cs2;
    return std::nullopt;
}

// lexeme_pick over a binary list head: the token with the binary's part before a separator, or none when the binary
// holds none (it joins the token).
std::optional<Picked> pick_binary_head(ProcessContext &context, Term &cs0, const Seps &seps, Token &token) {
    const Terms terms(context);
    const auto bin = need(cs0.head());
    const auto cont0 = need(cs0.tail());
    const auto found = bin_search(context, bin, {cont0}, seps);
    if (!found.matched) {
        token.binary(bin);
        cs0 = cont0;
        return std::nullopt;
    }
    const auto left = need(found.rest.head());
    return Picked{.lexeme = token.btoken(terms.slice(bin, 0, byte_size(bin) - byte_size(left))), .rest = found.rest};
}

// lexeme_pick over a plain list: the next code point handled, or the token at the end.
std::optional<Picked> pick_list(ProcessContext &context, Term &cs0, const Seps &seps, Token &token) {
    const auto next = point_of(context, cs0);
    if (!next) {
        return Picked{.lexeme = token.rev(), .rest = Terms(context).nil()};
    }
    return pick_point(context, cs0, *next, seps, token);
}

// lexeme_pick over a plain binary: the token up to the first separator, or the whole binary.
Picked pick_binary(ProcessContext &context, const Term &cs0, const Seps &seps, const Token &token) {
    const Terms terms(context);
    const auto found = bin_search(context, cs0, {terms.nil()}, seps);
    if (!found.matched) {
        return {.lexeme = token.btoken(cs0), .rest = terms.nil()};
    }
    const auto left = need(found.rest.head());
    return {.lexeme = token.btoken(terms.slice(cs0, 0, byte_size(cs0) - byte_size(left))), .rest = left};
}

// lexeme_pick: the lexeme from the start of the chardata and what follows its ending separator.
Picked lexeme_pick(ProcessContext &context, Term cs0, const Seps &seps) {
    const Terms terms(context);
    Token token(terms);
    while (true) {
        std::optional<Picked> done;
        switch (form_of(cs0)) {
        case Form::point:
            done = pick_point(context, cs0, split(cs0), seps, token);
            break;
        case Form::binary_head:
            done = pick_binary_head(context, cs0, seps, token);
            break;
        case Form::list:
            done = pick_list(context, cs0, seps, token);
            break;
        case Form::binary:
            return pick_binary(context, cs0, seps, token);
        default:
            no_clause();
        }
        if (done) {
            return *done;
        }
    }
}

// lexeme_skip's step at a code point: past a separator cluster (done), else on.
std::optional<Term> skip_point(ProcessContext &context, Term &cs0, const unicode::Next &next, const Seps &seps) {
    if (!member(seps.points, next.item)) {
        cs0 = next.rest;
        return std::nullopt;
    }
    const auto [gc, cs2] = cluster_of(context, cs0);
    if (member(seps.clusters, gc)) {
        return cs2;
    }
    cs0 = cs2;
    return std::nullopt;
}

// lexeme_skip over a binary list head: past the separator found in it, or on to the list after it.
std::optional<Term> skip_binary_head(ProcessContext &context, Term &cs0, const Seps &seps) {
    require_compiled(seps);
    const auto found = bin_search(context, need(cs0.head()), {need(cs0.tail())}, seps);
    if (found.matched) {
        return cluster_of(context, found.rest).rest;
    }
    cs0 = need(cs0.tail());
    return std::nullopt;
}

// lexeme_skip over a plain list: the next code point handled, or [] at the end.
std::optional<Term> skip_list(ProcessContext &context, Term &cs0, const Seps &seps) {
    const auto next = point_of(context, cs0);
    if (!next) {
        return Terms(context).nil();
    }
    return skip_point(context, cs0, *next, seps);
}

// lexeme_skip over a plain binary: past the separator found in it, or <<>>.
Term skip_binary(ProcessContext &context, const Term &cs0, const Seps &seps) {
    const Terms terms(context);
    require_compiled(seps);
    const auto found = bin_search(context, cs0, {terms.nil()}, seps);
    return found.matched ? cluster_of(context, need(found.rest.head())).rest : terms.binary({});
}

// lexeme_skip: the chardata after the lexeme at its start and the separator ending it.
Term lexeme_skip(ProcessContext &context, Term cs0, const Seps &seps) {
    while (true) {
        std::optional<Term> done;
        switch (form_of(cs0)) {
        case Form::point:
            done = skip_point(context, cs0, split(cs0), seps);
            break;
        case Form::binary_head:
            done = skip_binary_head(context, cs0, seps);
            break;
        case Form::list:
            done = skip_list(context, cs0, seps);
            break;
        case Form::binary:
            return skip_binary(context, cs0, seps);
        default:
            no_clause();
        }
        if (done) {
            return *done;
        }
    }
}

// The lexeme walks' step over a cluster at the start: a separator skipped, else a lexeme picked.
void take_lexeme(ProcessContext &context, Term &cs0, const Seps &seps, std::vector<Term> &tokens) {
    const auto picked = lexeme_pick(context, cs0, seps);
    tokens.push_back(picked.lexeme);
    cs0 = picked.rest;
}

// lexemes_m at a code point: a separator cluster is skipped, else a lexeme is picked.
void lexemes_point(ProcessContext &context, Term &cs0, const Seps &seps, std::vector<Term> &tokens) {
    if (member(seps.points, need(cs0.head())) && member(seps.clusters, cluster_of(context, cs0).item)) {
        cs0 = cluster_of(context, cs0).rest;
    } else {
        take_lexeme(context, cs0, seps, tokens);
    }
}

// lexemes_m over a binary list head: its leading separators skipped, a lexeme picked where one starts.
void lexemes_binary_head(ProcessContext &context, Term &cs0, const Seps &seps, std::vector<Term> &tokens) {
    const auto found = bin_search_inv(context, need(cs0.head()), {need(cs0.tail())}, {seps.list});
    cs0 = found.rest;
    if (found.matched) {
        take_lexeme(context, cs0, seps, tokens);
    }
}

// lexemes_m over a plain list: a separator cluster skipped or a lexeme picked; the tokens at [].
std::optional<Term> lexemes_list(ProcessContext &context, Term &cs0, const Seps &seps, std::vector<Term> &tokens) {
    const auto next = gc_term(context, cs0);
    if (next.is_nil()) {
        return Terms(context).list(tokens);
    }
    if (!next.is_cons()) {
        case_clause(next);
    }
    if (member(seps.clusters, need(next.head()))) {
        cs0 = need(next.tail());
    } else {
        take_lexeme(context, cs0, seps, tokens);
    }
    return std::nullopt;
}

// lexemes_m over a plain binary: the tokens when only separators remain; else a non-empty lexeme added.
std::optional<Term> lexemes_binary(ProcessContext &context, Term &cs0, const Seps &seps, std::vector<Term> &tokens) {
    const Terms terms(context);
    const auto found = bin_search_inv(context, cs0, {terms.nil()}, {seps.list});
    if (!found.matched) {
        return terms.list(tokens);
    }
    const auto picked = lexeme_pick(context, need(found.rest.head()), seps);
    if (!empty_binary(picked.lexeme)) {
        tokens.push_back(picked.lexeme);
    }
    cs0 = picked.rest;
    return std::nullopt;
}

// lexemes_m: the lexemes of the chardata, in order.
Term lexemes_of(ProcessContext &context, Term cs0, const Seps &seps) {
    std::vector<Term> tokens;
    while (true) {
        std::optional<Term> done;
        switch (form_of(cs0)) {
        case Form::point:
            lexemes_point(context, cs0, seps, tokens);
            break;
        case Form::binary_head:
            lexemes_binary_head(context, cs0, seps, tokens);
            break;
        case Form::list:
            done = lexemes_list(context, cs0, seps, tokens);
            break;
        case Form::binary:
            done = lexemes_binary(context, cs0, seps, tokens);
            break;
        default:
            no_clause();
        }
        if (done) {
            return *done;
        }
    }
}

Term lexemes2(ProcessContext &context, const TextArg &str, const PatternArg &seps) {
    if (str.term.is_nil()) {
        return str.term;
    }
    if (seps.term.is_nil()) {
        return Terms(context).list(std::array{str.term});
    }
    return lexemes_of(context, str.term, seps_of(context, seps.term));
}

// nth_lexeme_m at the start of a lexeme: the Nth from here, skipping N - 1.
std::optional<Term> nth_from(ProcessContext &context, Term &cs0, const Term &lexeme_start, const Seps &seps,
                             std::int64_t &n) {
    if (n > 1) {
        cs0 = lexeme_skip(context, lexeme_start, seps);
        --n;
        return std::nullopt;
    }
    require_compiled(seps);
    return lexeme_pick(context, lexeme_start, seps).lexeme;
}

// nth_lexeme_m over a binary list head: its leading separators skipped, the count advanced where a lexeme starts.
std::optional<Term> nth_binary_head(ProcessContext &context, Term &cs0, const Seps &seps, std::int64_t &n) {
    const auto found = bin_search_inv(context, need(cs0.head()), {need(cs0.tail())}, {seps.list});
    cs0 = found.rest;
    return found.matched ? nth_from(context, cs0, found.rest, seps, n) : std::nullopt;
}

// nth_lexeme_m over a plain list: a separator skipped, a lexeme skipped or the Nth picked; [] at the end.
std::optional<Term> nth_list(ProcessContext &context, Term &cs0, const Seps &seps, std::int64_t &n) {
    auto next = gc_term(context, cs0);
    if (next.is_nil()) {
        return next;
    }
    if (!next.is_cons()) {
        case_clause(next);
    }
    if (member(seps.clusters, need(next.head()))) {
        cs0 = need(next.tail());
        return std::nullopt;
    }
    if (n > 1) {
        cs0 = lexeme_skip(context, need(next.tail()), seps);
        --n;
        return std::nullopt;
    }
    require_compiled(seps);
    return lexeme_pick(context, cs0, seps).lexeme;
}

// nth_lexeme_m over a plain binary: <<>> when only separators remain, else the count advanced.
std::optional<Term> nth_binary(ProcessContext &context, Term &cs0, const Seps &seps, std::int64_t &n) {
    const Terms terms(context);
    require_compiled(seps);
    const auto found = bin_search_inv(context, cs0, {terms.nil()}, {seps.list});
    if (!found.matched) {
        return terms.binary({});
    }
    return nth_from(context, cs0, need(found.rest.head()), seps, n);
}

// nth_lexeme_m: the Nth lexeme of the chardata, or empty.
Term nth_lexeme_of(ProcessContext &context, Term cs0, const Seps &seps, std::int64_t n) {
    while (true) {
        std::optional<Term> done;
        switch (form_of(cs0)) {
        case Form::binary_head:
            done = nth_binary_head(context, cs0, seps, n);
            break;
        case Form::point:
        case Form::list:
            done = nth_list(context, cs0, seps, n);
            break;
        case Form::binary:
            done = nth_binary(context, cs0, seps, n);
            break;
        default:
            no_clause();
        }
        if (done) {
            return *done;
        }
    }
}

Term nth_lexeme3(ProcessContext &context, const TextArg &str, const Term &n, const PatternArg &seps) {
    const auto index = small_of(n);
    if (seps.term.is_nil() && index == 1) {
        return str.term;
    }
    if (!seps.term.is_list() || !index || *index <= 0) {
        no_clause();
    }
    return nth_lexeme_of(context, str.term, seps_of(context, seps.term), *index);
}

enum class Where : std::uint8_t { leading, trailing, all };

Where where_of(const Term &where) {
    const auto name = where.is_atom() ? need(where.atom_spelling()) : std::string_view{};
    if (name == "leading") {
        return Where::leading;
    }
    if (name == "trailing") {
        return Where::trailing;
    }
    if (name == "all") {
        return Where::all;
    }
    case_clause(where);
}

// split_1: the parts of a string around a needle. The state is OTP's: the current part reversed (Curr), the parts
// found (Acc) and the byte offset inside a leading binary (Start).
class Split final {
  public:
    Split(ProcessContext &context, const std::vector<Term> &needle, Where where)
        : context_(context), terms_(context), needle_(needle), where_(where), curr_(terms_.nil()), acc_(terms_.nil()) {}

    // split/3's answer for the string.
    Term run(const Term &string) {
        Term cs = string;
        while (true) {
            const auto step = next(cs);
            if (step == Step::leading) {
                return leading_;
            }
            if (step == Step::pair) {
                return finish(string);
            }
        }
    }

  private:
    enum class Step : std::uint8_t { more, pair, leading };

    Step next(Term &cs) {
        if (cs.is_cons() && need(cs.head()).is_integer()) {
            return at_point(cs, split(cs));
        }
        if (cs.is_cons() && need(cs.head()).is_binary()) {
            return at_binary_head(cs);
        }
        if (cs.is_list()) {
            const auto point = point_of(context_, cs);
            return point ? at_point(cs, *point) : Step::pair;
        }
        if (cs.is_binary()) {
            return at_binary(cs);
        }
        no_clause();
    }

    // The clauses of split/3 over split_1's {Curr, Acc}.
    Term finish(const Term &string) {
        if (acc_.is_nil()) {
            return terms_.list(std::array{string});
        }
        if (where_ == Where::trailing) {
            return acc_;
        }
        auto parts = *proper_list(acc_);
        parts.insert(parts.begin(), terms_.rev(curr_));
        std::ranges::reverse(parts);
        return terms_.list(parts);
    }

    // The needle found at `cs0` with `rest` after it: leading ends, trailing records and continues after the first
    // code point, all records and continues after the needle.
    Step matched(Term &cs, const Term &cs0, const Term &first, const Term &rest) {
        if (where_ == Where::leading) {
            leading_ = terms_.list(std::array{terms_.rev(curr_), rest});
            return Step::leading;
        }
        if (where_ == Where::trailing) {
            acc_ = terms_.list(std::array{terms_.rev(curr_), rest});
            curr_ = terms_.cons(first, curr_);
            cs = need(cp_term(context_, cs0).tail());
            return Step::more;
        }
        acc_ = terms_.cons(terms_.rev(curr_), acc_);
        curr_ = terms_.nil();
        cs = rest;
        return Step::more;
    }

    // A code point at the start: the needle may begin here.
    Step at_point(Term &cs, const unicode::Next &point) {
        if (same(point.item, needle_[0])) {
            if (const auto rest = after_prefix(context_, cs, needle_)) {
                return matched(cs, cs, point.item, *rest);
            }
        }
        curr_ = terms_.append(point.item, curr_);
        cs = point.rest;
        return Step::more;
    }

    // A binary list head searched from the offset, clusters continuing into the list after it.
    Step at_binary_head(Term &cs) {
        const auto bin = need(cs.head());
        const auto cont0 = need(cs.tail());
        const auto found = bin_search_str(context_, bin, start_, cont0, needle_);
        if (!found.matched) {
            curr_ = terms_.cons(terms_.slice(bin, 0, found.searched), curr_);
            cs = found.after;
            start_ = 0;
            return Step::more;
        }
        const auto curr = empty_binary(found.before) ? curr_ : terms_.cons(found.before, curr_);
        const auto [cs0, cont] = split(found.at);
        if (where_ == Where::leading) {
            leading_ = terms_.list(std::array{terms_.rev(curr), found.after});
            return Step::leading;
        }
        if (where_ == Where::trailing) {
            acc_ = terms_.list(std::array{terms_.rev(curr), found.after});
            start_ = byte_size(bin) - (byte_size(cs0) - first_char(cs0).second);
            cs = terms_.cons(bin, cont);
            return Step::more;
        }
        acc_ = terms_.cons(terms_.rev(curr), acc_);
        curr_ = terms_.nil();
        cs = found.after;
        start_ = 0;
        return Step::more;
    }

    // A plain binary searched from the offset.
    Step at_binary(Term &cs) {
        const auto found = bin_search_str(context_, cs, start_, terms_.nil(), needle_);
        if (!found.matched) {
            curr_ = terms_.cons(terms_.slice(cs, start_, byte_size(cs) - start_), curr_);
            return Step::pair;
        }
        const auto cs0 = need(found.at.head());
        if (where_ == Where::leading) {
            leading_ = terms_.list(std::array{terms_.rev(terms_.cons(found.before, curr_)), found.after});
            return Step::leading;
        }
        if (where_ == Where::trailing) {
            acc_ = terms_.list(std::array{terms_.btoken(found.before, {curr_}), found.after});
            start_ = byte_size(cs) - (byte_size(cs0) - first_char(cs0).second);
            return Step::more;
        }
        const auto keep = terms_.slice(found.before, start_, byte_size(found.before) - start_);
        acc_ = terms_.cons(terms_.rev(terms_.cons(keep, curr_)), acc_);
        curr_ = terms_.nil();
        start_ = byte_size(cs) - byte_size(found.after);
        return Step::more;
    }

    ProcessContext &context_;
    Terms terms_;
    const std::vector<Term> &needle_;
    Where where_;
    Term curr_;
    Term acc_;
    Term leading_{};
    std::size_t start_ = 0;
};

// The code points of a search pattern, as characters_to_list/1 gives them; function_clause for anything else.
std::vector<Term> needle_of(ProcessContext &context, const Term &pattern) {
    const auto chars = Terms(context).characters_to_list(pattern);
    const auto items = chars.is_list() ? proper_list(chars) : std::nullopt;
    if (!items || items->empty()) {
        no_clause();
    }
    return *items;
}

Term split3(ProcessContext &context, const TextArg &str, const PatternArg &pattern, const Term &where) {
    if (is_empty(pattern.term)) {
        return Terms(context).list(std::array{str.term});
    }
    const auto needle = needle_of(context, pattern.term);
    return Split(context, needle, where_of(where)).run(str.term);
}

Term split2(ProcessContext &context, const TextArg &str, const PatternArg &pattern) {
    return split3(context, str, pattern, Terms(context).atom("leading"));
}

// lists:join(Replacement, Parts).
Term joined(const Terms &terms, const OtherArg &replacement, const Term &parts) {
    std::vector<Term> items;
    for (const auto &part : *proper_list(parts)) {
        if (!items.empty()) {
            items.push_back(replacement.term);
        }
        items.push_back(part);
    }
    return terms.list(items);
}

Term replace4(ProcessContext &context, const TextArg &str, const PatternArg &pattern, const OtherArg &replacement,
              const Term &where) {
    return joined(Terms(context), replacement, split3(context, str, pattern, where));
}

Term replace3(ProcessContext &context, const TextArg &str, const PatternArg &pattern, const OtherArg &replacement) {
    return joined(Terms(context), replacement, split2(context, str, pattern));
}

// Whether the needle starts at a code point that begins `cs`.
bool needle_at(ProcessContext &context, const Term &cs, const Term &cp, const std::vector<Term> &needle) {
    return same(cp, needle[0]) && after_prefix(context, cs, needle).has_value();
}

// The needle at a code point that begins the chardata: the chardata itself when it matches, none otherwise; the
// walk continues after the code point.
std::optional<Term> find_point(ProcessContext &context, Term &cs, const unicode::Next &point,
                               const std::vector<Term> &needle) {
    const auto here = cs;
    cs = point.rest;
    return needle_at(context, here, point.item, needle) ? std::optional{here} : std::nullopt;
}

// find_l/find_r over a plain list: the code point at its start, none at [].
std::optional<unicode::Next> list_point(ProcessContext &context, const Term &cs) {
    return cs.is_cons() && need(cs.head()).is_integer() ? std::optional{split(cs)} : point_of(context, cs);
}

// find_l over a binary list head: the chardata from the needle found in it, else the walk moved on.
std::optional<Term> find_in_binary_head(ProcessContext &context, Term &cs, const std::vector<Term> &needle) {
    auto found = bin_search_str(context, need(cs.head()), 0, need(cs.tail()), needle);
    if (found.matched) {
        return found.at;
    }
    cs = found.after;
    return std::nullopt;
}

// find_l over a plain list: the chardata from a match at its first code point; nomatch at [].
std::optional<Term> find_in_list(ProcessContext &context, Term &cs, const std::vector<Term> &needle) {
    const auto point = list_point(context, cs);
    if (!point) {
        return Terms(context).atom("nomatch");
    }
    return find_point(context, cs, *point, needle);
}

// find_l over a plain binary: the slice from the needle, or nomatch.
Term find_in_binary(ProcessContext &context, const Term &cs, const std::vector<Term> &needle) {
    const Terms terms(context);
    const auto found = bin_search_str(context, cs, 0, terms.nil(), needle);
    return found.matched ? need(found.at.head()) : terms.atom("nomatch");
}

// One step of find_l: the answer, or none with the walk moved on.
std::optional<Term> find_leading_step(ProcessContext &context, Term &cs, const std::vector<Term> &needle) {
    switch (form_of(cs)) {
    case Form::binary_head:
        return find_in_binary_head(context, cs, needle);
    case Form::point:
    case Form::list:
        return find_in_list(context, cs, needle);
    case Form::binary:
        return find_in_binary(context, cs, needle);
    default:
        no_clause();
    }
}

// find_l: the chardata from the first occurrence of the needle, or nomatch.
Term find_leading(ProcessContext &context, Term cs, const std::vector<Term> &needle) {
    while (true) {
        if (const auto found = find_leading_step(context, cs, needle)) {
            return *found;
        }
    }
}

// find_r over a plain binary: the chardata from the needle found, continuing after its first code point.
std::optional<Term> find_binary(ProcessContext &context, Term &cs, const std::vector<Term> &needle) {
    const Terms terms(context);
    const auto found = bin_search_str(context, cs, 0, terms.nil(), needle);
    if (!found.matched) {
        return std::nullopt;
    }
    auto at = need(found.at.head());
    const auto first = first_char(at).second;
    cs = terms.slice(at, first, byte_size(at) - first);
    return at;
}

// find_r over a binary list head: the last match so far updated, the walk moved on.
void find_last_in_binary_head(ProcessContext &context, Term &cs, const std::vector<Term> &needle, Term &result) {
    const auto found = bin_search_str(context, need(cs.head()), 0, need(cs.tail()), needle);
    if (!found.matched) {
        cs = found.after;
        return;
    }
    result = found.at;
    cs = cluster_of(context, found.at).rest;
}

// find_r over a plain list: the last match so far updated; false at its end.
bool find_last_in_list(ProcessContext &context, Term &cs, const std::vector<Term> &needle, Term &result) {
    const auto point = list_point(context, cs);
    if (!point) {
        return false;
    }
    result = find_point(context, cs, *point, needle).value_or(result);
    return true;
}

// find_r over a plain binary: the last match so far updated; false when no more follow.
bool find_last_in_binary(ProcessContext &context, Term &cs, const std::vector<Term> &needle, Term &result) {
    const auto at = find_binary(context, cs, needle);
    if (!at) {
        return false;
    }
    result = *at;
    return true;
}

// find_r: the chardata from the last occurrence of the needle, or nomatch.
Term find_trailing(ProcessContext &context, Term cs, const std::vector<Term> &needle) {
    Term result = Terms(context).atom("nomatch");
    bool more = true;
    while (more) {
        switch (form_of(cs)) {
        case Form::binary_head:
            find_last_in_binary_head(context, cs, needle, result);
            break;
        case Form::point:
        case Form::list:
            more = find_last_in_list(context, cs, needle, result);
            break;
        case Form::binary:
            more = find_last_in_binary(context, cs, needle, result);
            break;
        default:
            no_clause();
        }
    }
    return result;
}

Term find3(ProcessContext &context, const TextArg &str, const PatternArg &pattern, const DirArg &dir) {
    if (pattern.term.is_nil() || empty_binary(pattern.term)) {
        return str.term;
    }
    const auto direction = dir.term.is_atom() ? need(dir.term.atom_spelling()) : std::string_view{};
    if (direction != "leading" && direction != "trailing") {
        no_clause();
    }
    const auto needle = needle_of(context, pattern.term);
    return direction == "leading" ? find_leading(context, str.term, needle) : find_trailing(context, str.term, needle);
}

Term find2(ProcessContext &context, const TextArg &str, const PatternArg &pattern) {
    return find3(context, str, pattern, {Terms(context).atom("leading")});
}

constexpr std::array STRING_LEXEME_BUILTINS{
    typed_entry<lexemes2>("string", "lexemes"), typed_entry<nth_lexeme3>("string", "nth_lexeme"),
    typed_entry<split2>("string", "split"),     typed_entry<split3>("string", "split"),
    typed_entry<replace3>("string", "replace"), typed_entry<replace4>("string", "replace"),
    typed_entry<find2>("string", "find"),       typed_entry<find3>("string", "find"),
};
} // namespace
} // namespace clause::runtime::builtins::string

namespace clause::runtime {
std::span<const BuiltinEntry> string_lexeme_builtins() noexcept { return builtins::string::STRING_LEXEME_BUILTINS; }
} // namespace clause::runtime
