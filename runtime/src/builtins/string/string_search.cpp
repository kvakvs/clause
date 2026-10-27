#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "../unicode/tables.hpp"
#include "search.hpp"
#include <algorithm>
#include <array>

// string's separator searches, trim and take (plan 21 step LIB4): string.erl's bin_search, bin_search_inv,
// bin_search_str, cp_prefix, trim_l/trim_t and take_l/take_lc/take_t/take_tc, clause for clause. The trailing
// walks, body-recursive in OTP, run as a loop recording one frame per step and fold the frames at the end.
namespace clause::runtime::builtins::string {
namespace {
using lists::no_clause;

// binary:match/2 of UTF-8 patterns from `start`: the byte position of the leftmost occurrence.
std::optional<std::size_t> match_from(std::span<const std::byte> bytes, std::size_t start,
                                      const std::vector<std::string> &patterns) {
    for (auto at = start; at < bytes.size(); ++at) {
        for (const auto &pattern : patterns) {
            const auto fits = pattern.size() <= bytes.size() - at;
            if (fits && std::equal(pattern.begin(), pattern.end(), reinterpret_cast<const char *>(bytes.data() + at))) {
                return at;
            }
        }
    }
    return std::nullopt;
}

std::size_t byte_size(const Term &binary) {
    if (!binary.is_binary()) {
        bad_argument();
    }
    return binary_bytes(binary).size();
}

// gc(Data) as a cons; a badmatch where OTP matches [GC | Rest] against another answer.
unicode::Next cluster_of(ProcessContext &context, const Term &data) {
    const auto next = gc_term(context, data);
    if (!next.is_cons()) {
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = next.word()};
    }
    return split(next);
}

// The slice of a binary from `from` to its end.
Term tail_from(const Terms &terms, const Term &binary, std::size_t from) {
    return terms.slice(binary, from, byte_size(binary) - from);
}

// Whether a cluster from the start of `bin` continues: the rest is a binary followed by the same `cont`.
std::optional<Term> continues_in(const Term &rest, const Term &cont) {
    if (rest.is_cons() && rest.head_unchecked().is_binary() && same(rest.tail_unchecked(), cont)) {
        return rest.head_unchecked();
    }
    return std::nullopt;
}

// bin_search_loop with an empty Cont: the matched separator's position, or where the search stops.
Found bin_search_alone(ProcessContext &context, const Term &bin0, const Seps &seps) {
    const Terms terms(context);
    std::size_t start = 0;
    while (true) {
        const auto bytes = binary_bytes(bin0);
        const auto where = bytes.size() <= start ? std::nullopt : match_from(bytes, start, seps.utf8);
        if (!where) {
            return {.matched = false, .rest = terms.nil()};
        }
        const auto cont1 = tail_from(terms, bin0, *where);
        const auto [gc, cont2] = cluster_of(context, cont1);
        if (member(seps.clusters, gc)) {
            const std::array one{cont1};
            return {.matched = true, .rest = terms.list(one)};
        }
        if (cont2.is_nil()) {
            return {.matched = false, .rest = terms.nil()};
        }
        start = bytes.size() - byte_size(cont2);
    }
}
} // namespace

Seps seps_of(ProcessContext &context, const Term &list) {
    const auto items = proper_list(list);
    if (!items) {
        no_clause();
    }
    Seps seps{.list = list, .clusters = {}, .points = {}, .utf8 = {}};
    for (const auto &item : *items) {
        auto point = item;
        if (!item.is_integer()) {
            const auto next = cp_term(context, item);
            if (!next.is_cons()) {
                throw BuiltinFailure{.reason = abi::v1::ErrorReason::badmatch, .payload = next.word()};
            }
            point = next.head_unchecked();
        }
        seps.clusters.push_back(item);
        seps.points.push_back(point);
        std::string text;
        encode(static_cast<char32_t>(*small_of(point)), text);
        seps.utf8.push_back(text);
    }
    return seps;
}

void require_compiled(const Seps &seps) {
    if (seps.utf8.empty()) {
        bad_argument();
    }
}

bool member(const std::vector<Term> &items, const Term &item) {
    return std::ranges::any_of(items, [&](const Term &candidate) { return same(candidate, item); });
}

bool member(const PatternArg &list, const Term &item) {
    Term rest = list.term;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        if (same(rest.head_unchecked(), item)) {
            return true;
        }
    }
    if (!rest.is_nil()) {
        bad_argument();
    }
    return false;
}

std::optional<std::vector<Term>> proper_list(const Term &list) {
    std::vector<Term> items;
    Term rest = list;
    for (; rest.is_cons(); rest = rest.tail_unchecked()) {
        items.push_back(rest.head_unchecked());
    }
    return rest.is_nil() ? std::optional{items} : std::nullopt;
}

Found bin_search(ProcessContext &context, const Term &bin, const ContArg &after, const Seps &seps) {
    const auto &cont = after.term;
    if (cont.is_nil()) {
        return bin_search_alone(context, bin, seps);
    }
    const Terms terms(context);
    std::size_t start = 0;
    while (true) {
        const auto bytes = binary_bytes(bin);
        const auto where = bytes.size() <= start ? std::nullopt : match_from(bytes, start, seps.utf8);
        if (!where) {
            return {.matched = false, .rest = cont};
        }
        const auto cont1 = terms.cons(tail_from(terms, bin, *where), cont);
        const auto [gc, cont2] = cluster_of(context, cont1);
        if (member(seps.clusters, gc)) {
            return {.matched = true, .rest = cont1};
        }
        const auto more = continues_in(cont2, cont);
        if (!more) {
            return {.matched = false, .rest = cont2};
        }
        start = bytes.size() - byte_size(*more);
    }
}

namespace {
// Whether bin_search_inv's binary is used up: [] or <<>>.
bool at_end(const Term &bin0) { return bin0.is_nil() || (bin0.is_binary() && byte_size(bin0) == 0); }

// The first code point of the binary and the slice after it; error({badarg, Bin}) unless it starts with a UTF-8
// character.
struct Head {
    Term cp1;
    Term rest;
};

Head head_of(ProcessContext &context, const Term &bin0) {
    const Terms terms(context);
    const auto head = bin0.is_binary() ? utf8_head(bin0) : std::nullopt;
    if (!head) {
        badarg_rest(context, bin0);
    }
    return {.cp1 = terms.integer(head->first), .rest = tail_from(terms, bin0, head->second)};
}

// The ASCII fast path of bin_search_inv: with a Latin-1 code point following, whether the first is a separator;
// none when the cluster rules must decide.
std::optional<bool> ascii_separator(ProcessContext &context, const Head &head, const PatternArg &clusters) {
    const auto second = utf8_head(head.rest);
    if (!second || !ascii_pair(head.cp1, Terms(context).integer(second->first))) {
        return std::nullopt;
    }
    return member(clusters, head.cp1);
}

// bin_search_inv_1/_n: the walk over the separator clusters at the start of a binary, with Cont after it (empty for
// a lone binary).
class InverseSearch final {
  public:
    InverseSearch(ProcessContext &context, const ContArg &cont, const PatternArg &clusters)
        : context_(context), terms_(context), cont_(cont.term), clusters_(clusters) {}

    Found run(Term bin0) const {
        while (!at_end(bin0)) {
            if (const auto found = step(bin0)) {
                return *found;
            }
        }
        return {.matched = false, .rest = cont_};
    }

  private:
    // One cluster at the start of bin0: the answer, or none with bin0 moved past a separator.
    std::optional<Found> step(Term &bin0) const {
        const auto head = head_of(context_, bin0);
        if (const auto separator = ascii_separator(context_, head, clusters_)) {
            if (!*separator) {
                return matched(bin0);
            }
            bin0 = head.rest;
            return std::nullopt;
        }
        return cont_.is_nil() ? cluster_alone(bin0) : cluster_continued(bin0);
    }

    // [Bin0 | Cont]: where the first cluster that is not a separator starts.
    Found matched(const Term &bin0) const { return {.matched = true, .rest = terms_.cons(bin0, cont_)}; }

    // A cluster of a lone binary read with gc.
    std::optional<Found> cluster_alone(Term &bin0) const {
        const auto next = gc_term(context_, bin0);
        if (!next.is_cons() || !member(clusters_, next.head_unchecked())) {
            return matched(bin0);
        }
        bin0 = next.tail_unchecked();
        return std::nullopt;
    }

    // A cluster that may continue into Cont: the walk goes on only while the rest is the binary's tail before Cont.
    std::optional<Found> cluster_continued(Term &bin0) const {
        const auto next = gc_term(context_, terms_.cons(bin0, cont_));
        if (!next.is_cons() || !member(clusters_, next.head_unchecked())) {
            return matched(bin0);
        }
        const auto more = continues_in(next.tail_unchecked(), cont_);
        if (!more) {
            return Found{.matched = false, .rest = next.tail_unchecked()};
        }
        bin0 = *more;
        return std::nullopt;
    }

    ProcessContext &context_;
    Terms terms_;
    Term cont_;
    PatternArg clusters_;
};
} // namespace

Found bin_search_inv(ProcessContext &context, const Term &bin, const ContArg &cont, const PatternArg &clusters) {
    return InverseSearch(context, cont, clusters).run(bin);
}

Term cp_prefix(ProcessContext &context, const Term &orig, const ContArg &after) {
    const auto &cont = after.term;
    const Terms terms(context);
    const auto until_rest = cp_term(context, cont);
    if (until_rest.is_nil()) {
        return orig;
    }
    const auto [until, rest_cont] = split(until_rest);
    std::vector<Term> taken;
    Term rest = orig;
    while (true) {
        const auto next = cp_term(context, rest);
        if (!next.is_cons()) {
            case_clause(next);
        }
        const auto [cp, more] = split(next);
        if (same(cp, until) && equal_terms(context, more, rest_cont)) {
            return terms.list(taken);
        }
        taken.push_back(cp);
        rest = more;
    }
}

namespace {
// The chardata after one code point of a needle that is not its last; none when another starts it.
std::optional<Term> after_point(ProcessContext &context, const Term &data, const Term &pre) {
    if (data.is_cons() && data.head_unchecked().is_integer()) {
        return same(data.head_unchecked(), pre) ? std::optional{data.tail_unchecked()} : std::nullopt;
    }
    const auto next = cp_term(context, data);
    return next.is_cons() && same(next.head_unchecked(), pre) ? std::optional{next.tail_unchecked()} : std::nullopt;
}
} // namespace

std::optional<Term> after_prefix(ProcessContext &context, Term data, const std::vector<Term> &needle) {
    if (needle.empty()) {
        return data;
    }
    for (std::size_t at = 0; at + 1 < needle.size(); ++at) {
        const auto rest = after_point(context, data, needle[at]);
        if (!rest) {
            return std::nullopt;
        }
        data = *rest;
    }
    // The last code point is matched as a whole cluster.
    const auto next = gc_term(context, data);
    return next.is_cons() && same(next.head_unchecked(), needle.back()) ? std::optional{next.tail_unchecked()}
                                                                        : std::nullopt;
}

namespace {
// The UTF-8 of a needle's code points.
std::string needle_utf8(const std::vector<Term> &needle) {
    std::string text;
    for (const auto &cp : needle) {
        encode(static_cast<char32_t>(*small_of(cp)), text);
    }
    return text;
}

Needle not_found(std::size_t searched, const Term &cont) {
    return {.matched = false, .before = cont, .at = cont, .after = cont, .searched = searched};
}

// The needle at byte `where` of bin0, Cont after the binary: the answer when it starts there, OTP's [Cs0 | Cont]
// at it.
std::optional<Needle> needle_at_byte(ProcessContext &context, const Term &bin0, std::size_t where, const Term &cont,
                                     const std::vector<Term> &needle) {
    const Terms terms(context);
    const auto cs0 = tail_from(terms, bin0, where);
    const auto rest = after_prefix(context, terms.stack(cs0, cont), needle);
    if (!rest) {
        return std::nullopt;
    }
    return Needle{.matched = true,
                  .before = terms.slice(bin0, 0, where),
                  .at = terms.cons(cs0, cont),
                  .after = rest->is_nil() ? terms.binary({}) : *rest,
                  .searched = where};
}

// bin_search_str_1: the whole needle searched for inside a binary with nothing after it.
Needle search_alone(ProcessContext &context, const Term &bin0, std::size_t start, const std::vector<Term> &needle) {
    const Terms terms(context);
    const auto whole = needle_utf8(needle);
    while (true) {
        const auto bytes = binary_bytes(bin0);
        const auto where = start > bytes.size() ? std::nullopt : match_from(bytes, start, {whole});
        if (!where) {
            return not_found(bytes.size(), terms.nil());
        }
        if (const auto found = needle_at_byte(context, bin0, *where, terms.nil(), needle)) {
            return *found;
        }
        start = *where + first_char(tail_from(terms, bin0, *where)).second;
    }
}

// bin_search_str_2: the needle's first code point searched for inside a binary that chardata follows.
Needle search_continued(ProcessContext &context, const Term &bin0, std::size_t start, const Term &cont,
                        const std::vector<Term> &needle) {
    const Terms terms(context);
    std::string first;
    encode(static_cast<char32_t>(*small_of(needle[0])), first);
    while (true) {
        const auto bytes = binary_bytes(bin0);
        const auto where = start > bytes.size() ? std::nullopt : match_from(bytes, start, {first});
        if (!where) {
            return not_found(bytes.size(), cont);
        }
        if (const auto found = needle_at_byte(context, bin0, *where, cont, needle)) {
            return *found;
        }
        const auto [gc, cs] = cluster_of(context, tail_from(terms, bin0, *where));
        if (!cs.is_binary()) {
            return not_found(*where, terms.stack(terms.cons(gc, cs), cont));
        }
        start = bytes.size() - byte_size(cs);
    }
}
} // namespace

Needle bin_search_str(ProcessContext &context, const Term &bin, std::size_t start, const Term &cont,
                      const std::vector<Term> &needle) {
    return cont.is_nil() ? search_alone(context, bin, start, needle)
                         : search_continued(context, bin, start, cont, needle);
}

namespace {
// trim_l over a plain list: the result when the walk ends at this cluster, none to continue after a separator.
std::optional<Term> trim_leading_list(ProcessContext &context, Term &str, const PatternArg &clusters) {
    auto next = gc_term(context, str);
    if (next.is_nil()) {
        return next;
    }
    if (!next.is_cons()) {
        case_clause(next);
    }
    if (!member(clusters, next.head_unchecked())) {
        return str;
    }
    str = next.tail_unchecked();
    return std::nullopt;
}

// trim_l over the first of an ASCII pair: a separator is skipped.
std::optional<Term> trim_leading_point(Term &str, const PatternArg &clusters) {
    if (!member(clusters, str.head_unchecked())) {
        return str;
    }
    str = str.tail_unchecked();
    return std::nullopt;
}

// trim_l over a binary list head: its separators skipped, into the list after it when it holds only those.
std::optional<Term> trim_leading_binary_head(ProcessContext &context, Term &str, const PatternArg &clusters) {
    auto found = bin_search_inv(context, str.head_unchecked(), {str.tail_unchecked()}, {clusters});
    if (found.matched) {
        return found.rest;
    }
    str = found.rest;
    return std::nullopt;
}

// trim_l over a plain binary: the slice after its separators.
Term trim_leading_binary(ProcessContext &context, const Term &str, const PatternArg &clusters) {
    const Terms terms(context);
    const auto found = bin_search_inv(context, str, {terms.nil()}, {clusters});
    return found.matched ? need(found.rest.head()) : terms.binary({});
}

// One step of trim_l: the result, or none with the string moved past a separator.
std::optional<Term> trim_leading_step(ProcessContext &context, Term &str, const PatternArg &clusters) {
    if (ascii_pair_at(str)) {
        return trim_leading_point(str, clusters);
    }
    switch (form_of(str)) {
    case Form::binary_head:
        return trim_leading_binary_head(context, str, clusters);
    case Form::point:
    case Form::list:
        return trim_leading_list(context, str, clusters);
    case Form::binary:
        return trim_leading_binary(context, str, clusters);
    default:
        no_clause();
    }
}

// trim_l: the chardata after its leading separator clusters.
Term trim_leading(ProcessContext &context, Term str, const PatternArg &clusters) {
    while (true) {
        if (const auto done = trim_leading_step(context, str, clusters)) {
            return *done;
        }
    }
}

// One step of a trailing walk, folded onto the result built from the end.
struct Frame {
    enum class Kind : std::uint8_t { point, cluster, checked, stack, keep_or_stack };
    Kind kind;
    // The code point, cluster or binary; for keep_or_stack the kept binary.
    Term term{};
    // keep_or_stack: the binary and the code points used after it.
    Term bin{};
    Term used{};
};

// trim_t folded: a point in front, a cluster appended (a separator one dropped before an empty rest), a binary
// stacked, or the kept part alone before an empty rest.
Term fold_trim(const Terms &terms, const std::vector<Frame> &frames, Term base) {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
        switch (it->kind) {
        case Frame::Kind::point:
            base = terms.cons(it->term, base);
            break;
        case Frame::Kind::cluster:
            base = terms.append(it->term, base);
            break;
        case Frame::Kind::checked:
            base = is_empty(base) ? terms.nil() : terms.append(it->term, base);
            break;
        case Frame::Kind::stack:
            base = terms.stack(it->term, base);
            break;
        case Frame::Kind::keep_or_stack:
            base = is_empty(base) ? it->term : terms.stack(it->bin, terms.stack(it->used, base));
            break;
        }
    }
    return base;
}

// The trailing walks' state: the chardata and the byte offset into a leading binary.
struct Cursor {
    Term str;
    std::size_t at;
};

// trim_t over a list head that is a code point: a separator's cluster is dropped before an empty rest.
void trim_point(ProcessContext &context, Cursor &cursor, const Seps &seps, std::vector<Frame> &frames) {
    const auto head = need(cursor.str.head());
    if (!member(seps.points, head)) {
        frames.push_back({.kind = Frame::Kind::point, .term = head});
        cursor.str = need(cursor.str.tail());
        return;
    }
    const auto [gc, rest] = cluster_of(context, cursor.str);
    frames.push_back({.kind = member(seps.clusters, gc) ? Frame::Kind::checked : Frame::Kind::cluster, .term = gc});
    cursor.str = rest;
}

// trim_t over a list head that is a binary: the separators found in it from the offset.
void trim_binary_head(ProcessContext &context, Cursor &cursor, const Seps &seps, std::vector<Frame> &frames) {
    const Terms terms(context);
    const auto bin = need(cursor.str.head());
    const auto cont0 = need(cursor.str.tail());
    const auto found = bin_search(context, tail_from(terms, bin, cursor.at), {cont0}, seps);
    if (!found.matched) {
        frames.push_back({.kind = Frame::Kind::stack, .term = bin});
        cursor = {.str = cont0, .at = 0};
        return;
    }
    const auto [sep_start, cont1] = split(found.rest);
    const auto inverse = bin_search_inv(context, sep_start, {cont1}, {seps.list});
    if (!inverse.matched) {
        const auto keep = terms.slice(bin, 0, byte_size(bin) - byte_size(sep_start));
        frames.push_back({.kind = Frame::Kind::keep_or_stack,
                          .term = keep,
                          .bin = bin,
                          .used = cp_prefix(context, cont0, {inverse.rest})});
        cursor = {.str = inverse.rest, .at = 0};
        return;
    }
    const auto [non_sep, cont] = split(inverse.rest);
    if (!non_sep.is_binary()) {
        case_clause(inverse.rest);
    }
    cursor = {.str = terms.cons(bin, cont), .at = byte_size(bin) - byte_size(non_sep)};
}

// trim_t over a plain binary: the result once the search ends; none while it continues at another offset.
std::optional<Term> trim_binary(ProcessContext &context, Cursor &cursor, const Seps &seps) {
    const Terms terms(context);
    const auto found = bin_search(context, tail_from(terms, cursor.str, cursor.at), {terms.nil()}, seps);
    if (!found.matched) {
        return cursor.str;
    }
    const auto sep_start = need(found.rest.head());
    const auto inverse = bin_search_inv(context, sep_start, {terms.nil()}, {seps.list});
    if (!inverse.matched) {
        return terms.slice(cursor.str, 0, byte_size(cursor.str) - byte_size(sep_start));
    }
    cursor.at = byte_size(cursor.str) - byte_size(need(inverse.rest.head()));
    return std::nullopt;
}

// trim_t over a plain list: the next cluster as a frame; [] ends the walk (returned as the base).
std::optional<Term> trim_list(ProcessContext &context, Cursor &cursor, const Seps &seps, std::vector<Frame> &frames) {
    auto next = gc_term(context, cursor.str);
    if (next.is_nil()) {
        return next;
    }
    if (!next.is_cons()) {
        case_clause(next);
    }
    const auto [gc, rest] = split(next);
    frames.push_back({.kind = member(seps.clusters, gc) ? Frame::Kind::checked : Frame::Kind::cluster, .term = gc});
    cursor.str = rest;
    return std::nullopt;
}

// One step of trim_t: the base the frames fold onto once the walk ends, none while it goes on.
std::optional<Term> trim_trailing_step(ProcessContext &context, Cursor &cursor, const Seps &seps,
                                       std::vector<Frame> &frames) {
    switch (form_of(cursor.str)) {
    case Form::point:
        trim_point(context, cursor, seps, frames);
        return std::nullopt;
    case Form::binary_head:
        trim_binary_head(context, cursor, seps, frames);
        return std::nullopt;
    case Form::list:
        return trim_list(context, cursor, seps, frames);
    case Form::binary:
        return trim_binary(context, cursor, seps);
    default:
        no_clause();
    }
}

// trim_t: the chardata without its trailing separator clusters.
Term trim_trailing(ProcessContext &context, const Term &str, const Seps &seps) {
    const Terms terms(context);
    std::vector<Frame> frames;
    Cursor cursor{.str = str, .at = 0};
    while (true) {
        if (const auto base = trim_trailing_step(context, cursor, seps, frames)) {
            return fold_trim(terms, frames, *base);
        }
    }
}

std::string_view direction_of(const Term &dir) {
    const auto name = dir.is_atom() ? dir.atom_spelling_unchecked() : std::string_view{};
    if (name != "leading" && name != "trailing" && name != "both") {
        no_clause();
    }
    return name;
}

Term trim3(ProcessContext &context, const TextArg &str, const DirArg &dir, const PatternArg &sep) {
    const auto direction = direction_of(dir.term);
    if (sep.term.is_nil()) {
        return str.term;
    }
    if (!sep.term.is_list()) {
        no_clause();
    }
    auto result = str.term;
    if (direction != "trailing") {
        result = trim_leading(context, result, sep);
    }
    if (direction != "leading") {
        result = trim_trailing(context, result, seps_of(context, sep.term));
    }
    return result;
}

// unicode_util:pattern_whitespace/0: [[$\r, $\n] | the single code points].
Term pattern_whitespace(ProcessContext &context) {
    const Terms terms(context);
    std::vector<Term> items{terms.list(std::array{terms.integer('\r'), terms.integer('\n')})};
    for (const auto code : unicode_tables::pattern_whitespace()) {
        items.push_back(terms.integer(static_cast<std::int64_t>(code)));
    }
    return terms.list(items);
}

Term trim2(ProcessContext &context, const TextArg &str, const DirArg &dir) {
    return trim3(context, str, dir, {pattern_whitespace(context)});
}

Term trim1(ProcessContext &context, const TextArg &str) {
    return trim3(context, str, {Terms(context).atom("both"_atom)}, {pattern_whitespace(context)});
}

Term chomp1(ProcessContext &context, const TextArg &str) {
    const Terms terms(context);
    const std::array seps{terms.list(std::array{terms.integer('\r'), terms.integer('\n')}), terms.integer('\n')};
    return trim3(context, str, {terms.atom("trailing"_atom)}, {terms.list(seps)});
}

using Pair = Halves;

Term pair_term(const Terms &terms, const Pair &pair) {
    const std::array items{pair.head, pair.tail};
    return terms.tuple(items);
}

// The leading takes' accumulator: OTP's reversed Acc, a code point or cluster prepended, a binary pushed.
class Acc final {
  public:
    explicit Acc(const Terms &terms) : terms_(terms), acc_(terms.nil()) {}

    void point(const Term &cp) { acc_ = terms_.cons(cp, acc_); }

    // append(rev(GC), Acc).
    void cluster(const Term &gc) { acc_ = terms_.append(terms_.rev(gc), acc_); }

    void binary(const Term &bin) { acc_ = terms_.cons(bin, acc_); }

    Term rev() const { return terms_.rev(acc_); }

    Term btoken(const Term &part) const { return terms_.btoken(part, {acc_}); }

  private:
    const Terms &terms_;
    Term acc_;
};

// characters_to_binary([Bin | Used]).
Term joined(const Terms &terms, const Term &bin, const ContArg &used) {
    return terms.characters_to_binary(terms.cons(bin, used.term));
}

// take_l and take_lc over a binary list head: the token so far with the binary's part, or the walk continued.
std::optional<Pair> take_binary_head(ProcessContext &context, Term &str, Acc &acc, const Found &found) {
    const Terms terms(context);
    const auto bin = need(str.head());
    const auto cont0 = need(str.tail());
    if (!found.matched) {
        acc.binary(joined(terms, bin, {cp_prefix(context, cont0, {found.rest})}));
        str = found.rest;
        return std::nullopt;
    }
    const auto bin1 = need(found.rest.head());
    if (!bin1.is_binary()) {
        case_clause(found.rest);
    }
    return Pair{.head = acc.btoken(terms.slice(bin, 0, byte_size(bin) - byte_size(bin1))), .tail = found.rest};
}

// take_l and take_lc over a plain binary.
Pair take_binary(ProcessContext &context, const Term &bin, const Acc &acc, const Found &found) {
    const Terms terms(context);
    if (!found.matched) {
        return {.head = acc.btoken(bin), .tail = terms.binary({})};
    }
    const auto after = need(found.rest.head());
    return {.head = acc.btoken(terms.slice(bin, 0, byte_size(bin) - byte_size(after))), .tail = after};
}

// take_l and take_lc over a plain list: the pair when the walk ends at this cluster (`stops` tells which clusters
// end the run), none to continue with it in the token.
template <typename Stops>
std::optional<Pair> take_list_cluster(ProcessContext &context, Term &str, Acc &acc, Stops stops) {
    const auto next = gc_term(context, str);
    if (next.is_nil()) {
        return Pair{.head = acc.rev(), .tail = next};
    }
    if (!next.is_cons()) {
        case_clause(next);
    }
    if (stops(next.head_unchecked())) {
        return Pair{.head = acc.rev(), .tail = str};
    }
    acc.cluster(next.head_unchecked());
    str = next.tail_unchecked();
    return std::nullopt;
}

// take_l over the first of an ASCII pair: a separator joins the token, another character ends the run.
std::optional<Pair> take_l_point(Term &str, Acc &acc, const PatternArg &clusters) {
    if (!member(clusters, str.head_unchecked())) {
        return Pair{.head = acc.rev(), .tail = str};
    }
    acc.point(str.head_unchecked());
    str = str.tail_unchecked();
    return std::nullopt;
}

// One step of take_l: the pair, or none with the token grown.
std::optional<Pair> take_l_step(ProcessContext &context, Term &str, Acc &acc, const PatternArg &clusters) {
    const auto stops = [&](const Term &gc) { return !member(clusters, gc); };
    if (ascii_pair_at(str)) {
        return take_l_point(str, acc, clusters);
    }
    switch (form_of(str)) {
    case Form::binary_head:
        return take_binary_head(context, str, acc,
                                bin_search_inv(context, str.head_unchecked(), {str.tail_unchecked()}, {clusters}));
    case Form::point:
    case Form::list:
        return take_list_cluster(context, str, acc, stops);
    case Form::binary:
        return take_binary(context, str, acc, bin_search_inv(context, str, {Terms(context).nil()}, {clusters}));
    default:
        no_clause();
    }
}

// take_l: the leading separator clusters and the rest.
Pair take_l(ProcessContext &context, Term str, const PatternArg &clusters) {
    const Terms terms(context);
    Acc acc(terms);
    while (true) {
        if (const auto done = take_l_step(context, str, acc, clusters)) {
            return *done;
        }
    }
}

// take_lc at a code point: a separator cluster ends the run; else the point or its cluster joins the token.
std::optional<Pair> take_lc_point(ProcessContext &context, Term &str, Acc &acc, const Seps &seps) {
    const auto cp1 = str.head_unchecked();
    if (!member(seps.points, cp1)) {
        acc.point(cp1);
        str = str.tail_unchecked();
        return std::nullopt;
    }
    const auto [gc, rest] = cluster_of(context, str);
    if (member(seps.clusters, gc)) {
        return Pair{.head = acc.rev(), .tail = str};
    }
    acc.cluster(gc);
    str = rest;
    return std::nullopt;
}

// One step of take_lc: the pair, or none with the token grown.
std::optional<Pair> take_lc_step(ProcessContext &context, Term &str, Acc &acc, const Seps &seps) {
    const auto stops = [&](const Term &gc) { return member(seps.clusters, gc); };
    switch (form_of(str)) {
    case Form::point:
        return take_lc_point(context, str, acc, seps);
    case Form::binary_head:
        return take_binary_head(context, str, acc,
                                bin_search(context, str.head_unchecked(), {str.tail_unchecked()}, seps));
    case Form::list:
        return take_list_cluster(context, str, acc, stops);
    case Form::binary:
        return take_binary(context, str, acc, bin_search(context, str, {Terms(context).nil()}, seps));
    default:
        no_clause();
    }
}

// take_lc: the leading clusters that are not separators, and the rest.
Pair take_leading_complement(ProcessContext &context, Term str, const Seps &seps) {
    const Terms terms(context);
    Acc acc(terms);
    while (true) {
        if (const auto done = take_lc_step(context, str, acc, seps)) {
            return *done;
        }
    }
}

// One step of take_t or take_tc folded onto the {Head, Tail} pair built from the end.
struct PairFrame {
    enum class Kind : std::uint8_t { point, cluster, checked, stack, keep_or_stack };
    Kind kind;
    // The code point or cluster, or the binary to stack; for keep_or_stack the kept binary.
    Term term{};
    // keep_or_stack: the end of the binary after the kept part, the code points used, and the whole binary with
    // them.
    Term end{};
    Term used{};
    Term whole{};
};

// A checked cluster folded: in front of the tail before an empty head, else of the head.
Pair fold_checked(const Terms &terms, const PairFrame &frame, Pair base) {
    if (is_empty(base.head)) {
        base.tail = terms.append(frame.term, base.tail);
    } else {
        base.head = terms.append(frame.term, base.head);
    }
    return base;
}

// A binary's kept part folded: alone with its end stacked on the tail before an empty head, else the whole binary
// stacked on the head.
Pair fold_kept(const Terms &terms, const PairFrame &frame, Pair base) {
    if (is_empty(base.head)) {
        return {.head = frame.term, .tail = terms.stack(terms.stack(frame.end, frame.used), base.tail)};
    }
    base.head = terms.stack(frame.whole, base.head);
    return base;
}

Pair fold_frame(const Terms &terms, const PairFrame &frame, Pair base) {
    switch (frame.kind) {
    case PairFrame::Kind::point:
        base.head = terms.cons(frame.term, base.head);
        return base;
    case PairFrame::Kind::cluster:
        base.head = terms.append(frame.term, base.head);
        return base;
    case PairFrame::Kind::checked:
        return fold_checked(terms, frame, base);
    case PairFrame::Kind::stack:
        base.head = terms.stack(frame.term, base.head);
        return base;
    case PairFrame::Kind::keep_or_stack:
        return fold_kept(terms, frame, base);
    }
    return base;
}

Pair fold_take(const Terms &terms, const std::vector<PairFrame> &frames, Pair base) {
    for (auto it = frames.rbegin(); it != frames.rend(); ++it) {
        base = fold_frame(terms, *it, base);
    }
    return base;
}

// The trailing takes differ in which clusters count: separators (take_t) or the others (take_tc).
struct Trailing {
    const Seps &seps;
    bool complement;

    // Whether a cluster splits the head from the tail when nothing follows it.
    bool splits(const Term &gc) const { return member(seps.clusters, gc) != complement; }

    Found first_run(ProcessContext &context, const Term &bin, const ContArg &cont) const {
        return complement ? bin_search_inv(context, bin, {cont}, {seps.list}) : bin_search(context, bin, {cont}, seps);
    }

    Found second_run(ProcessContext &context, const Term &bin, const ContArg &cont) const {
        return complement ? bin_search(context, bin, {cont}, seps) : bin_search_inv(context, bin, {cont}, {seps.list});
    }
};

void take_cluster(const Trailing &walk, const unicode::Next &next, Cursor &cursor, std::vector<PairFrame> &frames) {
    frames.push_back(
        {.kind = walk.splits(next.item) ? PairFrame::Kind::checked : PairFrame::Kind::cluster, .term = next.item});
    cursor.str = next.rest;
}

// take_t/take_tc over a binary list head.
void take_binary_head_trailing(ProcessContext &context, const Trailing &walk, Cursor &cursor,
                               std::vector<PairFrame> &frames) {
    const Terms terms(context);
    const auto bin = need(cursor.str.head());
    const auto cont0 = need(cursor.str.tail());
    const auto first = walk.first_run(context, tail_from(terms, bin, cursor.at), {cont0});
    if (!first.matched) {
        frames.push_back(
            {.kind = PairFrame::Kind::stack, .term = joined(terms, bin, {cp_prefix(context, cont0, {first.rest})})});
        cursor = {.str = first.rest, .at = 0};
        return;
    }
    const auto [sep_start, cont1] = split(first.rest);
    const auto second = walk.second_run(context, sep_start, {cont1});
    if (!second.matched) {
        const auto keep_size = byte_size(bin) - byte_size(sep_start);
        const auto used = cp_prefix(context, cont0, {second.rest});
        frames.push_back({.kind = PairFrame::Kind::keep_or_stack,
                          .term = terms.slice(bin, 0, keep_size),
                          .end = tail_from(terms, bin, keep_size),
                          .used = used,
                          .whole = joined(terms, bin, {used})});
        cursor = {.str = second.rest, .at = 0};
        return;
    }
    const auto [non_sep, cont] = split(second.rest);
    if (!non_sep.is_binary()) {
        case_clause(second.rest);
    }
    cursor = {.str = terms.cons(bin, cont), .at = byte_size(bin) - byte_size(non_sep)};
}

// take_t/take_tc over a plain binary: the pair once the search ends; none while it continues at another offset.
std::optional<Pair> take_binary_trailing(ProcessContext &context, const Trailing &walk, Cursor &cursor) {
    const Terms terms(context);
    const auto first = walk.first_run(context, tail_from(terms, cursor.str, cursor.at), {terms.nil()});
    if (!first.matched) {
        return Pair{.head = cursor.str, .tail = terms.binary({})};
    }
    const auto sep_start = need(first.rest.head());
    const auto second = walk.second_run(context, sep_start, {terms.nil()});
    if (!second.matched) {
        const auto keep_size = byte_size(cursor.str) - byte_size(sep_start);
        return Pair{.head = terms.slice(cursor.str, 0, keep_size), .tail = tail_from(terms, cursor.str, keep_size)};
    }
    cursor.at = byte_size(cursor.str) - byte_size(need(second.rest.head()));
    return std::nullopt;
}

// take_t/take_tc at a code point: take_t keeps a point outside the separators and reads a separator's cluster;
// take_tc takes the first of an ASCII pair as a cluster and reads the cluster otherwise.
void take_point_trailing(ProcessContext &context, const Trailing &walk, Cursor &cursor,
                         std::vector<PairFrame> &frames) {
    const auto head = need(cursor.str.head());
    const auto tail = need(cursor.str.tail());
    if (walk.complement && ascii_pair_at(cursor.str)) {
        frames.push_back(
            {.kind = walk.splits(head) ? PairFrame::Kind::checked : PairFrame::Kind::cluster, .term = head});
        cursor.str = tail;
        return;
    }
    if (!walk.complement && !member(walk.seps.points, head)) {
        frames.push_back({.kind = PairFrame::Kind::point, .term = head});
        cursor.str = tail;
        return;
    }
    take_cluster(walk, cluster_of(context, cursor.str), cursor, frames);
}

// take_t/take_tc over a plain list: the next cluster as a frame; [] ends the walk with the empty pair.
std::optional<Pair> take_list_trailing(ProcessContext &context, const Trailing &walk, Cursor &cursor,
                                       std::vector<PairFrame> &frames) {
    const auto next = gc_term(context, cursor.str);
    if (next.is_nil()) {
        return Pair{.head = next, .tail = next};
    }
    if (!next.is_cons()) {
        case_clause(next);
    }
    take_cluster(walk, split(next), cursor, frames);
    return std::nullopt;
}

// One step of take_t or take_tc: the base the frames fold onto once the walk ends, none while it goes on.
std::optional<Pair> take_trailing_step(ProcessContext &context, const Trailing &walk, Cursor &cursor,
                                       std::vector<PairFrame> &frames) {
    switch (form_of(cursor.str)) {
    case Form::point:
        take_point_trailing(context, walk, cursor, frames);
        return std::nullopt;
    case Form::binary_head:
        take_binary_head_trailing(context, walk, cursor, frames);
        return std::nullopt;
    case Form::list:
        return take_list_trailing(context, walk, cursor, frames);
    case Form::binary:
        return take_binary_trailing(context, walk, cursor);
    default:
        no_clause();
    }
}

// take_t and take_tc: the chardata before its trailing run of clusters, and that run.
Pair take_trailing(ProcessContext &context, const Term &str, const Trailing &walk) {
    const Terms terms(context);
    std::vector<PairFrame> frames;
    Cursor cursor{.str = str, .at = 0};
    while (true) {
        if (const auto base = take_trailing_step(context, walk, cursor, frames)) {
            return fold_take(terms, frames, *base);
        }
    }
}

// take/4's Complement and Dir arguments: function_clause for anything but booleans and leading | trailing.
struct TakeOptions {
    bool inverted;
    bool leading;
};

TakeOptions take_options(const Term &complement, const DirArg &dir) {
    const auto direction = dir.term.is_atom() ? dir.term.atom_spelling_unchecked() : std::string_view{};
    if (!complement.is_boolean() || (direction != "leading" && direction != "trailing")) {
        no_clause();
    }
    return {.inverted = complement.boolean_value_unchecked(), .leading = direction == "leading"};
}

// take(Str, [], Complement, Dir): the string and its empty counterpart, in the order the options give.
Pair empty_take(const Terms &terms, const Term &str, const TakeOptions &options) {
    const auto empty = str.is_binary() ? terms.binary({}) : terms.nil();
    return options.inverted == options.leading ? Pair{.head = str, .tail = empty} : Pair{.head = empty, .tail = str};
}

Term take4(ProcessContext &context, const TextArg &str, const PatternArg &sep, const Term &complement,
           const DirArg &dir) {
    const Terms terms(context);
    const auto options = take_options(complement, dir);
    if (sep.term.is_nil()) {
        return pair_term(terms, empty_take(terms, str.term, options));
    }
    if (options.leading && !options.inverted) {
        return pair_term(terms, take_l(context, str.term, sep));
    }
    const auto seps = seps_of(context, sep.term);
    if (options.leading) {
        return pair_term(terms, take_leading_complement(context, str.term, seps));
    }
    return pair_term(terms, take_trailing(context, str.term, {.seps = seps, .complement = options.inverted}));
}

Term take3(ProcessContext &context, const TextArg &str, const PatternArg &sep, const Term &complement) {
    return take4(context, str, sep, complement, {Terms(context).atom("leading"_atom)});
}

Term take2(ProcessContext &context, const TextArg &str, const PatternArg &sep) {
    return take3(context, str, sep, Terms(context).boolean(false));
}

constexpr std::array STRING_SEARCH_BUILTINS{
    typed_entry<trim1>("string", "trim"),   typed_entry<trim2>("string", "trim"), typed_entry<trim3>("string", "trim"),
    typed_entry<chomp1>("string", "chomp"), typed_entry<take2>("string", "take"), typed_entry<take3>("string", "take"),
    typed_entry<take4>("string", "take"),
};
} // namespace

Halves take_leading(ProcessContext &context, const Term &str, const PatternArg &clusters) {
    return take_l(context, str, clusters);
}

Term trim_of(ProcessContext &context, const Term &data) { return trim1(context, {data}); }

Term trim_leading_of(ProcessContext &context, const Term &data) {
    return trim2(context, {data}, {Terms(context).atom("leading"_atom)});
}
} // namespace clause::runtime::builtins::string

namespace clause::runtime {
std::span<const BuiltinEntry> string_search_builtins() noexcept { return builtins::string::STRING_SEARCH_BUILTINS; }
} // namespace clause::runtime
