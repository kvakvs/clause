#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "../unicode/normalize.hpp"
#include "search.hpp"
#include <algorithm>
#include <array>
#include <map>

// string's text functions (plan 21 step LIB4): is_empty, length, to_graphemes, reverse, slice, pad, the case
// mappings, prefix, equal, next_codepoint, next_grapheme and jaro_similarity, each walking chardata as string.erl's
// clauses do over cp/1 and gc/1.
namespace clause::runtime::builtins::string {
namespace {
using lists::no_clause;

// gc(Data) as a cons; [] ends the walk (none), an error raises badarg.
std::optional<unicode::Next> next_cluster(ProcessContext &context, const Term &data) {
    const auto next = gc_term(context, data);
    if (next.is_nil()) {
        return std::nullopt;
    }
    if (next.is_tuple()) {
        badarg_error(context, next);
    }
    return split(next);
}

// gc(Data) for chardata known to start with a cluster.
unicode::Next first_cluster(ProcessContext &context, const Term &data) {
    const auto cluster = next_cluster(context, data);
    if (!cluster) {
        bad_argument();
    }
    return *cluster;
}

// cp(Data) as a cons, likewise.
std::optional<unicode::Next> next_point(ProcessContext &context, const Term &data) {
    const auto next = cp_term(context, data);
    if (next.is_nil()) {
        return std::nullopt;
    }
    if (next.is_tuple()) {
        badarg_error(context, next);
    }
    return split(next);
}

Term is_empty1(ProcessContext &context, const Term &data) { return Terms(context).boolean(is_empty(data)); }

// length_b: clusters of a binary counted from its first code point, reading ahead with cp.
std::int64_t length_binary(ProcessContext &context, const Term &binary) {
    const Terms terms(context);
    const auto head = first_char(binary);
    auto cp1 = terms.integer(head.first);
    auto bin0 = terms.slice(binary, head.second, binary_bytes(binary).size() - head.second);
    std::int64_t count = 0;
    while (true) {
        const auto next = next_point(context, first_cluster(context, terms.cons(cp1, bin0)).rest);
        ++count;
        if (!next) {
            return count;
        }
        cp1 = next->item;
        bin0 = next->rest;
    }
}

Term length1(ProcessContext &context, const Term &data) {
    if (data.is_binary() && utf8_head(data)) {
        return Terms(context).integer(length_binary(context, data));
    }
    std::int64_t count = 0;
    Term rest = data;
    while (const auto cluster = next_cluster(context, rest)) {
        ++count;
        rest = cluster->rest;
    }
    return Terms(context).integer(count);
}

// Every cluster of the chardata, in order; badarg at invalid UTF-8.
std::vector<Term> clusters_of(ProcessContext &context, const Term &data) {
    std::vector<Term> clusters;
    Term rest = data;
    while (const auto cluster = next_cluster(context, rest)) {
        clusters.push_back(cluster->item);
        rest = cluster->rest;
    }
    return clusters;
}

Term to_graphemes1(ProcessContext &context, const Term &data) {
    return Terms(context).list(clusters_of(context, data));
}

Term reverse1(ProcessContext &context, const Term &data) {
    auto clusters = clusters_of(context, data);
    std::ranges::reverse(clusters);
    return Terms(context).list(clusters);
}

// slice_lb: the binary after N clusters from its first code point; <<>> when it ends first.
Term slice_binary(ProcessContext &context, const Term &binary, std::int64_t count) {
    const Terms terms(context);
    const auto head = first_char(binary);
    auto cp1 = terms.integer(head.first);
    auto bin = terms.slice(binary, head.second, binary_bytes(binary).size() - head.second);
    while (true) {
        auto rest = first_cluster(context, terms.cons(cp1, bin)).rest;
        if (count == 1) {
            return rest;
        }
        const auto next = next_point(context, rest);
        if (!next) {
            return terms.binary({});
        }
        cp1 = next->item;
        bin = next->rest;
        --count;
    }
}

// slice_l0: the chardata after N clusters, [] when it ends first.
Term slice_from(ProcessContext &context, const Term &data, std::int64_t count) {
    if (data.is_binary() && count > 0 && utf8_head(data)) {
        return slice_binary(context, data, count);
    }
    Term rest = data;
    for (; count > 0; --count) {
        const auto cluster = next_cluster(context, rest);
        if (!cluster) {
            return Terms(context).nil();
        }
        rest = cluster->rest;
    }
    return rest;
}

// [] from a binary is <<>>.
Term empty_as(ProcessContext &context, const Term &data, const Term &result) {
    return result.is_nil() && data.is_binary() ? Terms(context).binary({}) : result;
}

std::int64_t non_negative(const Term &term) {
    const auto value = small_of(term);
    if (!value || *value < 0) {
        no_clause();
    }
    return *value;
}

Term slice2(ProcessContext &context, const TextArg &data, const Term &start) {
    return empty_as(context, data.term, slice_from(context, data.term, non_negative(start)));
}

// slice_list: the first N clusters of a list, appended into a flat list.
Term slice_list(ProcessContext &context, const Term &data, std::int64_t count) {
    const Terms terms(context);
    std::vector<Term> clusters;
    Term rest = data;
    for (; count > 0; --count) {
        const auto cluster = next_cluster(context, rest);
        if (!cluster) {
            break;
        }
        clusters.push_back(cluster->item);
        rest = cluster->rest;
    }
    Term result = terms.nil();
    for (auto it = clusters.rbegin(); it != clusters.rend(); ++it) {
        result = terms.append(*it, result);
    }
    return result;
}

// slice_bin: the bytes of the binary after N clusters from its first code point, 0 when it ends first.
std::size_t bytes_after(ProcessContext &context, const Term &binary, std::int64_t count) {
    const Terms terms(context);
    const auto head = first_char(binary);
    auto cp1 = terms.integer(head.first);
    auto cd = terms.slice(binary, head.second, binary_bytes(binary).size() - head.second);
    std::size_t cp1_size = head.second;
    for (; count > 0; --count) {
        const auto rest = first_cluster(context, terms.cons(cp1, cd)).rest;
        const auto next = next_point(context, rest);
        if (!next) {
            return 0;
        }
        cp1 = next->item;
        cd = next->rest;
        std::string text;
        encode(static_cast<char32_t>(*small_of(cp1)), text);
        cp1_size = text.size();
    }
    return binary_bytes(cd).size() + cp1_size;
}

// slice_trail: the first N clusters of the chardata after the start.
Term slice_trail(ProcessContext &context, const Term &data, std::int64_t count) {
    if (!data.is_binary()) {
        return slice_list(context, data, count);
    }
    const Terms terms(context);
    const auto size = binary_bytes(data).size();
    if (size == 0) {
        return data;
    }
    if (!utf8_head(data)) {
        badarg_rest(context, data);
    }
    const auto after = bytes_after(context, data, count);
    return terms.slice(data, 0, size - after);
}

Term slice3(ProcessContext &context, const TextArg &data, const Term &start, const LengthArg &length_arg) {
    const auto &length = length_arg.term;
    const Terms terms(context);
    if (is_char(length, 0)) {
        return data.term.is_binary() ? terms.binary({}) : terms.nil();
    }
    const auto from = non_negative(start);
    if (length.is_atom() && need(length.atom_spelling()) == "infinity") {
        return empty_as(context, data.term, slice_from(context, data.term, from));
    }
    const auto count = small_of(length);
    if (!count || *count <= 0) {
        no_clause();
    }
    const auto rest = slice_from(context, data.term, from);
    if (rest.is_nil() && data.term.is_binary()) {
        return terms.binary({});
    }
    return slice_trail(context, rest, *count);
}

// pad/4: Char repeated to the length, before, after or around the chardata.
Term pad4(ProcessContext &context, const TextArg &data, const Term &length, const DirArg &dir, const Term &fill) {
    const Terms terms(context);
    const auto wanted = small_of(length);
    const auto direction = dir.term.is_atom() ? need(dir.term.atom_spelling()) : std::string_view{};
    if (!wanted || (direction != "leading" && direction != "trailing" && direction != "both")) {
        no_clause();
    }
    const auto have = *small_of(length1(context, data.term));
    const auto missing = static_cast<std::size_t>(std::max<std::int64_t>(0, *wanted - have));
    if (direction == "leading") {
        const std::array items{terms.list(std::vector<Term>(missing, fill)), data.term};
        return terms.list(items);
    }
    if (direction == "trailing") {
        return terms.cons(data.term, terms.list(std::vector<Term>(missing, fill)));
    }
    const auto side = terms.list(std::vector<Term>(missing / 2, fill));
    const auto post = missing % 2 == 1 ? terms.list(std::array{fill}) : terms.nil();
    const std::array items{side, data.term, side};
    return terms.list(items, post);
}

Term pad3(ProcessContext &context, const TextArg &data, const Term &length, const DirArg &dir) {
    return pad4(context, data, length, dir, Terms(context).integer(' '));
}

Term pad2(ProcessContext &context, const TextArg &data, const Term &length) {
    return pad3(context, data, length, {Terms(context).atom("trailing")});
}

// The case mappings: upper (0), lower (1), title (2), fold (3); the ASCII fast paths change a..z or A..Z.
struct Mapping {
    unicode::Case which;
    char32_t from;
    char32_t to;
    int delta;
};

constexpr std::array<Mapping, 4> MAPPINGS{{{unicode::Case::upper, 'a', 'z', -32},
                                           {unicode::Case::lower, 'A', 'Z', 32},
                                           {unicode::Case::title, 0, 0, 0},
                                           {unicode::Case::fold, 'A', 'Z', 32}}};

// Collects the mapped code points of one case walk and whether anything changed.
class Cased final {
  public:
    Cased(ProcessContext &context, const Mapping &mapping) : context_(context), mapping_(mapping) {}

    // One ASCII code point before a Latin-1 one: mapped by the fast path when in the mapping's range.
    void ascii(std::int64_t code) {
        if (code >= mapping_.from && code <= mapping_.to) {
            codes_.push_back(static_cast<char32_t>(code + mapping_.delta));
            changed_ = true;
        } else {
            codes_.push_back(static_cast<char32_t>(code));
        }
    }

    // A mapped cluster: a code point or a list of them.
    void mapped(const Term &cluster, bool changed) {
        codes_ += cluster_codes(cluster);
        changed_ = changed_ || changed;
    }

    bool changed() const { return changed_; }

    const std::u32string &codes() const { return codes_; }

    // unicode_util's mapping of the first code point of `data`.
    Term map_first(const Term &data) const { return unicode::mapped_first(context_, data, mapping_.which); }

  private:
    ProcessContext &context_;
    const Mapping &mapping_;
    std::u32string codes_;
    bool changed_ = false;
};

// The case walks' ASCII fast path over a list: the first code point when it is ASCII and a Latin-1 one follows.
std::optional<std::int64_t> ascii_before_latin1(const Term &rest) {
    if (!rest.is_cons() || !need(rest.tail()).is_cons()) {
        return std::nullopt;
    }
    const auto first = small_of(need(rest.head()));
    const auto second = small_of(need(need(rest.tail()).head()));
    const bool ascii = first && *first >= 0 && *first < 128;
    const bool latin1 = second && *second >= 0 && *second < 256;
    return ascii && latin1 ? first : std::nullopt;
}

// uppercase_list and its siblings over a list: the flat list of mapped code points, or none when nothing changed.
std::optional<Term> cased_list(ProcessContext &context, const Term &data, const Mapping &mapping) {
    Cased cased(context, mapping);
    Term rest = data;
    while (true) {
        if (const auto first = ascii_before_latin1(rest)) {
            cased.ascii(*first);
            rest = need(rest.tail());
            continue;
        }
        const auto next = cased.map_first(rest);
        if (next.is_nil()) {
            break;
        }
        const auto [mapped, more] = split(next);
        cased.mapped(mapped, !(rest.is_cons() && same(mapped, need(rest.head()))));
        rest = more;
    }
    if (!cased.changed()) {
        return std::nullopt;
    }
    std::vector<Term> items;
    for (const auto code : cased.codes()) {
        items.push_back(Terms(context).integer(static_cast<std::int64_t>(code)));
    }
    return Terms(context).list(items);
}

// uppercase_bin and its siblings over a binary from its first code point: the mapped code points, or none when
// nothing changed.
// The binary case walks' position: the current code point and the binary after it.
struct BinaryWalk {
    Term cp1;
    Term bin;
};

// The ASCII fast path of the binary case walks: an ASCII code point before a Latin-1 one mapped, the walk moved on.
bool ascii_step(const Terms &terms, Cased &cased, BinaryWalk &walk) {
    const auto second = utf8_head(walk.bin);
    const auto first = *small_of(walk.cp1);
    if (!second || first < 0 || first >= 128 || second->first >= 256) {
        return false;
    }
    cased.ascii(first);
    walk.cp1 = terms.integer(second->first);
    walk.bin = terms.slice(walk.bin, second->second, binary_bytes(walk.bin).size() - second->second);
    return true;
}

// The mapped code points when anything changed.
std::optional<std::u32string> changed_codes(const Cased &cased) {
    return cased.changed() ? std::optional{cased.codes()} : std::nullopt;
}

std::optional<std::u32string> cased_binary(ProcessContext &context, const Term &binary, const Mapping &mapping) {
    const Terms terms(context);
    Cased cased(context, mapping);
    const auto head = first_char(binary);
    BinaryWalk walk{.cp1 = terms.integer(head.first),
                    .bin = terms.slice(binary, head.second, binary_bytes(binary).size() - head.second)};
    while (true) {
        if (ascii_step(terms, cased, walk)) {
            continue;
        }
        const auto [mapped, cps] = split(cased.map_first(terms.cons(walk.cp1, walk.bin)));
        const bool kept = same(mapped, walk.cp1);
        const auto next = cp_term(context, cps);
        if (next.is_tuple()) {
            badarg_error(context, next);
        }
        cased.mapped(mapped, !kept);
        if (next.is_nil()) {
            return changed_codes(cased);
        }
        const auto [point, more] = split(next);
        walk = {.cp1 = point, .bin = more};
    }
}

Term cased(ProcessContext &context, const Term &data, const Mapping &mapping) {
    const Terms terms(context);
    if (data.is_list()) {
        return cased_list(context, data, mapping).value_or(data);
    }
    if (data.is_binary() && utf8_head(data)) {
        const auto codes = cased_binary(context, data, mapping);
        return codes ? terms.utf8(*codes) : data;
    }
    if (data.is_binary() && binary_bytes(data).empty()) {
        return data;
    }
    badarg_rest(context, data);
}

Term uppercase1(ProcessContext &context, const Term &data) { return cased(context, data, MAPPINGS[0]); }

Term lowercase1(ProcessContext &context, const Term &data) { return cased(context, data, MAPPINGS[1]); }

Term casefold1(ProcessContext &context, const Term &data) { return cased(context, data, MAPPINGS[3]); }

// titlecase/1: the first code point's title case in front of the rest.
Term titlecase1(ProcessContext &context, const Term &data) {
    const Terms terms(context);
    if (!data.is_list() && !data.is_binary()) {
        no_clause();
    }
    const auto next = unicode::mapped_first(context, data, unicode::Case::title);
    if (next.is_nil()) {
        return data.is_binary() ? terms.binary({}) : next;
    }
    const auto [cluster, rest] = split(next);
    if (data.is_list()) {
        return terms.append(cluster, rest);
    }
    if (!rest.is_binary()) {
        bad_argument();
    }
    std::string text;
    for (const auto code : cluster_codes(cluster)) {
        encode(code, text);
    }
    auto bytes = std::vector<std::byte>(std::as_bytes(std::span(text)).begin(), std::as_bytes(std::span(text)).end());
    const auto tail = need(rest.binary_bytes());
    bytes.insert(bytes.end(), tail.begin(), tail.end());
    return terms.binary(bytes);
}

Term prefix2(ProcessContext &context, const TextArg &data, const PatternArg &prefix) {
    const Terms terms(context);
    const auto chars = terms.characters_to_list(prefix.term);
    if (!chars.is_list()) {
        no_clause();
    }
    std::vector<Term> codes;
    for (Term rest = chars; rest.is_cons(); rest = need(rest.tail())) {
        codes.push_back(need(rest.head()));
    }
    Term result = data.term;
    if (!codes.empty()) {
        const auto after = after_prefix(context, data.term, codes);
        if (!after) {
            return terms.atom("nomatch");
        }
        result = *after;
    }
    return empty_as(context, data.term, result);
}

// A transform applied before comparing code points: a normalization form and/or casefold.
struct Transform {
    std::optional<unicode::Form> form;
    bool fold;
};

Term transformed(ProcessContext &context, const Term &data, const Transform &transform) {
    auto result = data;
    if (transform.form) {
        result = unicode::normalized_term(context, result, *transform.form);
    }
    if (transform.fold) {
        result = unicode::mapped_first(context, result, unicode::Case::fold);
    }
    return cp_term(context, result);
}

// The two strings string:equal compares, walked together.
struct Sides {
    Term a;
    Term b;
};

// One step of equal_1 or its normalizing siblings, `read` giving each side's cp answer: equal or not when both end
// or differ, none to go on with the rests (left in the sides); {case_clause, {A, B}} where cp gives an error.
template <typename Read> std::optional<bool> compare_step(ProcessContext &context, Sides &sides, Read read) {
    const auto ca = read(sides.a);
    const auto cb = read(sides.b);
    if (ca.is_cons() && cb.is_cons()) {
        if (!same(need(ca.head()), need(cb.head()))) {
            return false;
        }
        sides = {.a = need(ca.tail()), .b = need(cb.tail())};
        return std::nullopt;
    }
    if (ca.is_list() && cb.is_list()) {
        return ca.is_nil() && cb.is_nil();
    }
    const std::array pair{ca, cb};
    case_clause(Terms(context).tuple(pair));
}

// equal_nocase and equal_norm: the same term is equal; else code point by code point after the transform.
bool equal_transformed(ProcessContext &context, const Term &a, const Term &b, const Transform &transform) {
    const auto read = [&](const Term &side) { return transformed(context, side, transform); };
    Sides sides{.a = a, .b = b};
    while (!same(sides.a, sides.b)) {
        if (const auto answer = compare_step(context, sides, read)) {
            return *answer;
        }
    }
    return true;
}

Term equal2(ProcessContext &context, const TextArg &a, const OtherArg &b) {
    if (a.term.is_binary() && b.term.is_binary()) {
        return Terms(context).boolean(same(a.term, b.term));
    }
    return Terms(context).boolean(equal_terms(context, a.term, b.term));
}

bool boolean_of(const Term &term) {
    if (!term.is_boolean()) {
        no_clause();
    }
    return need(term.boolean_value());
}

Term equal3(ProcessContext &context, const TextArg &a, const OtherArg &b, const Term &ignore_case) {
    if (!boolean_of(ignore_case)) {
        return equal2(context, a, b);
    }
    return Terms(context).boolean(equal_transformed(context, a.term, b.term, {.form = std::nullopt, .fold = true}));
}

std::optional<unicode::Form> form_of(const Term &norm) {
    constexpr std::array<std::pair<std::string_view, unicode::Form>, 4> FORMS{{{"nfc", unicode::Form::nfc},
                                                                               {"nfd", unicode::Form::nfd},
                                                                               {"nfkc", unicode::Form::nfkc},
                                                                               {"nfkd", unicode::Form::nfkd}}};
    const auto name = norm.is_atom() ? need(norm.atom_spelling()) : std::string_view{};
    const auto found = std::ranges::find(FORMS, name, &std::pair<std::string_view, unicode::Form>::first);
    return found == FORMS.end() ? std::nullopt : std::optional{found->second};
}

Term equal4(ProcessContext &context, const TextArg &a, const OtherArg &b, const Term &ignore_case,
            const OptionArg &norm_arg) {
    const auto &norm = norm_arg.term;
    if (norm.is_atom() && need(norm.atom_spelling()) == "none") {
        return equal3(context, a, b, ignore_case);
    }
    const bool fold = boolean_of(ignore_case);
    const auto form = form_of(norm);
    if (!form) {
        // unicode_util:Norm/1 for another atom: undef.
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = Terms(context).atom("undef").word()};
    }
    return Terms(context).boolean(equal_transformed(context, a.term, b.term, {.form = form, .fold = fold}));
}

Term next_codepoint1(ProcessContext &context, const Term &data) { return cp_term(context, data); }

Term next_grapheme1(ProcessContext &context, const Term &data) { return gc_term(context, data); }

// jaro_similarity/2: the Jaro similarity of two chardata by grapheme clusters, as string.erl computes it.
class Jaro final {
  public:
    Jaro(ProcessContext &context, const Term &a, const Term &b)
        : a_(keys(clusters_of(context, a))), b_(keys(clusters_of(context, b))) {}

    double run() {
        if (a_.empty() && b_.empty()) {
            return 1.0;
        }
        if (a_.empty() || b_.empty()) {
            return 0.0;
        }
        match();
        if (matches_a_.empty()) {
            return 0.0;
        }
        const auto [m, t] = transpositions();
        const auto matched = static_cast<double>(m);
        return (matched / static_cast<double>(a_.size()) + matched / static_cast<double>(b_.size()) +
                (matched - static_cast<double>(t) / 2) / matched) /
               3;
    }

  private:
    using Key = std::u32string;

    static std::vector<Key> keys(const std::vector<Term> &clusters) {
        std::vector<Key> result;
        result.reserve(clusters.size());
        for (const auto &cluster : clusters) {
            result.push_back(cluster_codes(cluster));
        }
        return result;
    }

    // jaro_match: each cluster of A matched to the first unmatched equal cluster of B within the distance; A's
    // matches kept newest first, B's by descending index (add_rsorted).
    void match() {
        std::map<Key, std::vector<std::size_t>> positions;
        for (std::size_t index = 0; index < b_.size(); ++index) {
            positions[b_[index]].push_back(index);
        }
        const auto distance = static_cast<std::ptrdiff_t>(std::max<std::size_t>(1, std::max(a_.size(), b_.size()) / 2));
        for (std::size_t index = 0; index < a_.size(); ++index) {
            auto &candidates = positions[a_[index]];
            const auto found = detect(candidates, static_cast<std::ptrdiff_t>(index) - distance,
                                      static_cast<std::ptrdiff_t>(index) + distance);
            if (!found) {
                continue;
            }
            const auto position = candidates[*found];
            candidates.erase(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(*found) + 1);
            matches_a_.insert(matches_a_.begin(), a_[index]);
            const auto place =
                std::ranges::find_if(matches_b_, [&](const auto &entry) { return position > entry.first; });
            matches_b_.insert(place, {position, a_[index]});
        }
    }

    // jaro_detect: the first index strictly inside (low, high); indexes below high but not above low are skipped.
    static std::optional<std::size_t> detect(const std::vector<std::size_t> &candidates, std::ptrdiff_t low,
                                             std::ptrdiff_t high) {
        for (std::size_t at = 0; at < candidates.size(); ++at) {
            const auto index = static_cast<std::ptrdiff_t>(candidates[at]);
            if (low < index && index < high) {
                return at;
            }
            if (index >= high) {
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // jaro_calc_mt: the matches paired off; a pair of different clusters is a transposition.
    std::pair<std::size_t, std::size_t> transpositions() const {
        std::size_t t = 0;
        for (std::size_t at = 0; at < matches_a_.size(); ++at) {
            t += matches_a_[at] != matches_b_[at].second ? 1 : 0;
        }
        return {matches_a_.size(), t};
    }

    std::vector<Key> a_;
    std::vector<Key> b_;
    std::vector<Key> matches_a_;
    std::vector<std::pair<std::size_t, Key>> matches_b_;
};

Term jaro_similarity2(ProcessContext &context, const TextArg &a, const OtherArg &b) {
    return need(TermFactory(context).floating(Jaro(context, a.term, b.term).run()));
}

constexpr std::array STRING_TEXT_BUILTINS{
    typed_entry<is_empty1>("string", "is_empty"),
    typed_entry<length1>("string", "length"),
    typed_entry<to_graphemes1>("string", "to_graphemes"),
    typed_entry<reverse1>("string", "reverse"),
    typed_entry<slice2>("string", "slice"),
    typed_entry<slice3>("string", "slice"),
    typed_entry<pad2>("string", "pad"),
    typed_entry<pad3>("string", "pad"),
    typed_entry<pad4>("string", "pad"),
    typed_entry<uppercase1>("string", "uppercase"),
    typed_entry<lowercase1>("string", "lowercase"),
    typed_entry<titlecase1>("string", "titlecase"),
    typed_entry<casefold1>("string", "casefold"),
    typed_entry<prefix2>("string", "prefix"),
    typed_entry<equal2>("string", "equal"),
    typed_entry<equal3>("string", "equal"),
    typed_entry<equal4>("string", "equal"),
    typed_entry<next_codepoint1>("string", "next_codepoint"),
    typed_entry<next_grapheme1>("string", "next_grapheme"),
    typed_entry<jaro_similarity2>("string", "jaro_similarity"),
};
} // namespace

// equal_1: code point by code point; {case_clause, {A, B}} where cp gives an error.
namespace {
// Two lists starting with code points: the fast clause of equal_1.
bool point_heads(const Term &a, const Term &b) {
    return a.is_cons() && b.is_cons() && need(a.head()).is_integer() && need(b.head()).is_integer();
}

// The answer once equal_1 reads both with cp, as compare_step gives it.
std::optional<bool> compare_points(ProcessContext &context, Sides &sides) {
    return compare_step(context, sides, [&](const Term &side) { return cp_term(context, side); });
}

bool equal_points(ProcessContext &context, const Term &a, const Term &b) {
    Sides sides{.a = a, .b = b};
    while (true) {
        if (point_heads(sides.a, sides.b)) {
            if (!same(need(sides.a.head()), need(sides.b.head()))) {
                return false;
            }
            sides = {.a = need(sides.a.tail()), .b = need(sides.b.tail())};
        } else if (sides.a.is_nil()) {
            return is_empty(sides.b);
        } else if (const auto answer = compare_points(context, sides)) {
            return *answer;
        }
    }
}
} // namespace

bool equal_terms(ProcessContext &context, const Term &a, const Term &b) {
    if (a.is_binary() && b.is_binary()) {
        return same(a, b);
    }
    return equal_points(context, a, b);
}

Term length_of(ProcessContext &context, const Term &data) { return length1(context, data); }

Term prefix_of(ProcessContext &context, const Term &data, std::int64_t length) {
    const Terms terms(context);
    return slice3(context, {data}, terms.integer(0), {terms.integer(length)});
}
} // namespace clause::runtime::builtins::string

namespace clause::runtime {
std::span<const BuiltinEntry> string_text_builtins() noexcept { return builtins::string::STRING_TEXT_BUILTINS; }
} // namespace clause::runtime
