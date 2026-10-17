#include "code_points.hpp"
#include "../../terms/bitstrings.hpp"
#include "../lists/list_walk.hpp"
#include "../text.hpp"
#include "tables.hpp"
#include <algorithm>
#include <array>
#include <vector>

// unicode_util:cp/1 and gc/1 over terms (plan 21 step LIB4): a clause-for-clause port of Clause's unicode_util.erl,
// which follows OTP's shapes. cp opens nested lists one code point at a time (up to three more code points flattened
// after the first); gc applies the grapheme cluster rules in OTP's order, reading ahead with cp. A binary's rest is
// a slice sharing its buffer, so walks over long binaries stay linear.
namespace clause::runtime::builtins::unicode {
namespace {
using lists::no_clause;
namespace tables = unicode_tables;

constexpr std::uint32_t CONTROL = 32;
constexpr std::uint32_t EXTEND = 64;
constexpr std::uint32_t ZWJ = 128;
constexpr std::uint32_t PREPEND = 256;
constexpr std::uint32_t REGIONAL = 512;
constexpr std::uint32_t HANGUL_L = 1024;
constexpr std::uint32_t HANGUL_V = 2048;
constexpr std::uint32_t HANGUL_T = 4096;
constexpr std::uint32_t HANGUL_LV = 8192;
constexpr std::uint32_t HANGUL_LVT = 16384;
constexpr std::uint32_t EXT_PICT = 32768;
constexpr std::uint32_t CONSONANT = 65536;
constexpr std::uint32_t LINKER = 131072;

// An integer term's value when it fits a small integer.
std::optional<std::int64_t> small_of(const Term &term) {
    const auto value = term.is_integer() ? term.integer_value() : std::unexpected(TermError::wrong_type);
    return value ? std::optional{*value} : std::nullopt;
}

bool is_cp(const Term &term) {
    const auto value = small_of(term);
    return value && *value >= 0 && *value <= 0x10FFFF;
}

bool is_latin1(const Term &term) {
    const auto value = small_of(term);
    return value && *value >= 0 && *value <= 255;
}

bool is(const Term &term, std::int64_t code) { return small_of(term) == code; }

// The property bits of a code point term the walk has checked.
std::uint32_t flags(const Term &cp) { return flags_of(static_cast<char32_t>(*small_of(cp))); }

// Term building over one process.
class Build final {
  public:
    explicit Build(ProcessContext &context) : context_(context) {}

    Term nil() const { return need(TermFactory(context_).nil()); }

    Term cons(const Term &head, const Term &tail) const { return need(TermFactory(context_).cons(head, tail)); }

    Term integer(std::int64_t value) const { return need(TermFactory(context_).integer(value)); }

    Term list(std::span<const Term> items) const { return need(TermFactory(context_).list(items)); }

    // {error, Rest}.
    Term error(const Term &rest) const {
        const std::array items{need(TermFactory(context_).atom("error")), rest};
        return need(TermFactory(context_).tuple(items));
    }

    // The first UTF-8 character of a binary and the slice after it; none for an empty or invalid binary.
    std::optional<Next> utf8_head(const Term &binary) const {
        const auto bytes = detail::trusted_binary_bytes(binary.word());
        if (!bytes || bytes->empty()) {
            return std::nullopt;
        }
        std::size_t at = 0;
        const auto code = next_utf8(*bytes, at);
        if (!code) {
            return std::nullopt;
        }
        const auto size = bytes->size();
        const auto rest = need(detail::BitAccess::slice(context_.heap(), binary, at * 8, (size - at) * 8));
        return Next{.item = integer(static_cast<std::int64_t>(*code)), .rest = rest};
    }

    static bool empty_binary(const Term &binary) { return need(binary.bit_size()) == 0; }

  private:
    ProcessContext &context_;
};

// unicode_util's cp/1, cpl/2 and their continuations.
class CodePoints final {
  public:
    explicit CodePoints(ProcessContext &context) : b_(context) {}

    // cp(Chardata).
    Term cp(const Term &data) {
        if (data.is_cons()) {
            const auto head = data.head_unchecked();
            const auto tail = data.tail_unchecked();
            if (is_cp(head)) {
                return data;
            }
            return tail.is_nil() ? cp(head) : cpl(head, tail);
        }
        if (data.is_nil()) {
            return data;
        }
        if (!data.is_binary()) {
            no_clause();
        }
        if (const auto next = b_.utf8_head(data)) {
            return b_.cons(next->item, next->rest);
        }
        return Build::empty_binary(data) ? b_.nil() : b_.error(data);
    }

    // The rest after a code point taken from a nested list whose tail is `tail`, with `rest` after the list (the
    // [C | cpl_cont(T, R)] and [C | cpl_1_cont(R)] clauses).
    Term after(const Term &tail, const Term &rest) { return tail.is_nil() ? one_cont(1, rest) : cont(1, tail, rest); }

  private:
    // cpl(List, R): the first code point of List with R after it.
    Term cpl(const Term &list, const Term &rest) {
        if (list.is_cons()) {
            const auto head = list.head_unchecked();
            const auto tail = list.tail_unchecked();
            if (is_cp(head)) {
                return b_.cons(head, after(tail, rest));
            }
            return tail.is_nil() ? cpl(head, rest) : cpl(head, b_.cons(tail, rest));
        }
        if (list.is_nil()) {
            return cp(rest);
        }
        if (!list.is_binary()) {
            no_clause();
        }
        if (const auto next = b_.utf8_head(list)) {
            return b_.cons(next->item, b_.cons(next->rest, rest));
        }
        return Build::empty_binary(list) ? cp(rest) : b_.error(b_.cons(list, rest));
    }

    // cpl_cont/cpl_cont2/cpl_cont3 (level 1 to 3): the rest after a code point taken from a nested list, up to three
    // more code points flattened in front of R.
    Term cont(int level, const Term &list, const Term &rest) {
        if (list.is_nil()) {
            return one_cont(level, rest);
        }
        if (!list.is_cons()) {
            return b_.cons(list, rest);
        }
        const auto head = list.head_unchecked();
        const auto tail = list.tail_unchecked();
        if (head.is_integer()) {
            if (level < 3) {
                return b_.cons(head, cont(level + 1, tail, rest));
            }
            return tail.is_nil() ? b_.cons(head, rest) : b_.cons(head, b_.cons(tail, rest));
        }
        return tail.is_nil() ? cont(level, head, rest) : cont(level, head, b_.cons(tail, rest));
    }

    // cpl_1_cont/cpl_1_cont2/cpl_1_cont3: the same without an R.
    Term one_cont(int level, const Term &list) {
        if (!list.is_cons()) {
            return list;
        }
        const auto head = list.head_unchecked();
        const auto tail = list.tail_unchecked();
        if (head.is_integer()) {
            return level < 3 ? b_.cons(head, one_cont(level + 1, tail)) : list;
        }
        return tail.is_nil() ? one_cont(level, head) : cont(level, head, tail);
    }

    Build b_;
};

// unicode_util's gc/1 and its cluster rules.
class Clusters final {
  public:
    explicit Clusters(ProcessContext &context) : context_(context), b_(context), cps_(context) {}

    Term gc(const Term &data) {
        if (data.is_nil()) {
            return data;
        }
        if (data.is_cons()) {
            return gc_list(data);
        }
        return data.is_binary() ? gc_binary(data) : gc_other(data);
    }

  private:
    // gc(<<CP1/utf8, ...>>): two Latin-1 code points (the first not CR) leave the first alone.
    Term gc_binary(const Term &data) {
        const auto next = b_.utf8_head(data);
        if (!next) {
            return Build::empty_binary(data) ? b_.nil() : gc_other(data);
        }
        if (latin1_pair(*next)) {
            return b_.cons(next->item, next->rest);
        }
        return gc_1(next->rest, next->item);
    }

    // Whether a binary's first code point is Latin-1 but not CR and a Latin-1 code point follows it.
    bool latin1_pair(const Next &next) const {
        if (!is_latin1(next.item) || is(next.item, '\r')) {
            return false;
        }
        const auto after = b_.utf8_head(next.rest);
        return after && is_latin1(after->item);
    }

    Term cp(const Term &data) { return cps_.cp(data); }

    // gc(Str) for anything else: cp first, then gc of that.
    Term gc_other(const Term &data) {
        const auto cps = cp(data);
        return cps.is_tuple() ? cps : gc(cps);
    }

    Term gc_list(const Term &data) {
        const auto head = data.head_unchecked();
        const auto tail = data.tail_unchecked();
        if (is_cp(head) && tail.is_nil()) {
            return data;
        }
        if (is(head, '\r')) {
            const auto next = cp(tail);
            return next.is_cons() && is(next.head_unchecked(), '\n') ? crlf(next.tail_unchecked()) : data;
        }
        if (is_latin1(head)) {
            return gc_latin1_list(data, {.item = head, .rest = tail});
        }
        return is_cp(head) ? gc_1(tail, head) : gc_other(data);
    }

    // gc([CP1 | T1]) for a Latin-1 CP1: the pair stays when a Latin-1 code point follows without a binary.
    Term gc_latin1_list(const Term &data, const Next &parts) {
        const auto &head = parts.item;
        const auto &tail = parts.rest;
        if (tail.is_cons() && is_latin1(tail.head_unchecked())) {
            return data;
        }
        const auto next = cp_no_bin(tail);
        if (!next) {
            return gc_1(tail, head);
        }
        if (next->is_cons() && is_latin1(next->head_unchecked())) {
            return b_.cons(head, *next);
        }
        return gc_1(*next, head);
    }

    // cp_no_bin: the first code point of a list and the rest, or none (binary_found) where a binary would have to
    // be decoded.
    std::optional<Term> cp_no_bin(const Term &data) {
        if (data.is_cons()) {
            const auto head = data.head_unchecked();
            const auto tail = data.tail_unchecked();
            if (head.is_integer()) {
                return data;
            }
            return tail.is_nil() ? cp_no_bin(head) : cp_no_binl(head, tail);
        }
        return data.is_nil() ? std::optional{data} : std::nullopt;
    }

    std::optional<Term> cp_no_binl(const Term &list, const Term &rest) {
        if (list.is_cons()) {
            const auto head = list.head_unchecked();
            const auto tail = list.tail_unchecked();
            if (head.is_integer()) {
                return b_.cons(head, cps_.after(tail, rest));
            }
            return tail.is_nil() ? cp_no_binl(head, rest) : cp_no_binl(head, b_.cons(tail, rest));
        }
        return list.is_nil() ? cp_no_bin(rest) : std::nullopt;
    }

    Term crlf(const Term &rest) {
        const std::array pair{b_.integer('\r'), b_.integer('\n')};
        return b_.cons(b_.list(pair), rest);
    }

    // gc_1(R0, CP): the cluster starting with CP, followed by R0.
    Term gc_1(const Term &rest, const Term &cp) {
        if (is(cp, '\r')) {
            const auto next = this->cp(rest);
            return next.is_cons() && is(next.head_unchecked(), '\n') ? crlf(next.tail_unchecked()) : b_.cons(cp, rest);
        }
        if (!is_cp(cp)) {
            TermFactory factory(context_);
            const std::array reason{need(factory.atom("badarg")), cp};
            throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                                 .payload = need(factory.tuple(reason)).word()};
        }
        return is_latin1(cp) ? gc_latin1(rest, cp, flags(cp)) : gc_class(rest, cp, flags(cp));
    }

    Term gc_latin1(const Term &rest, const Term &cp, std::uint32_t bits) {
        if ((bits & CONTROL) != 0 || (rest.is_cons() && is_latin1(rest.head_unchecked()) && (bits & EXT_PICT) == 0)) {
            return b_.cons(cp, rest);
        }
        if ((bits & EXT_PICT) != 0) {
            return gc_ext_pict(rest, {cp});
        }
        return gc_extend({.next = this->cp(rest), .rest = rest}, cp);
    }

    Term gc_class(const Term &rest, const Term &cp, std::uint32_t bits) {
        if ((bits & CONTROL) != 0) {
            return b_.cons(cp, rest);
        }
        if ((bits & PREPEND) != 0) {
            return gc_prepend(rest, cp);
        }
        if ((bits & (HANGUL_L | HANGUL_V | HANGUL_T)) != 0) {
            return hangul_start(rest, cp, bits);
        }
        const auto code = *small_of(cp);
        if (code >= 44000 && code <= 56000) {
            const auto whole = b_.cons(cp, rest);
            return gc_h_lv_lvt(whole, whole, {});
        }
        if ((bits & REGIONAL) != 0) {
            return gc_regional(rest, cp);
        }
        if ((bits & EXT_PICT) != 0) {
            return gc_ext_pict(rest, {cp});
        }
        if ((bits & CONSONANT) != 0) {
            return gc_indic(this->cp(rest), rest, false, {cp});
        }
        return gc_extend({.next = this->cp(rest), .rest = rest}, cp);
    }

    Term hangul_start(const Term &rest, const Term &cp, std::uint32_t bits) {
        if ((bits & HANGUL_L) != 0) {
            return gc_h_l(rest, {cp});
        }
        return (bits & HANGUL_V) != 0 ? gc_h_v(rest, {cp}) : gc_h_t(rest, {cp});
    }

    // A prepended code point joins the cluster after it unless a control follows.
    Term gc_prepend(const Term &rest, const Term &cp0) {
        const auto next = this->cp(rest);
        if (next.is_nil()) {
            const std::array one{cp0};
            return b_.list(one);
        }
        if (next.is_tuple()) {
            return b_.cons(cp0, need(next.tuple_element(1)));
        }
        const auto [cp1, r0] = split(next);
        if ((flags(cp1) & CONTROL) != 0) {
            return b_.cons(cp0, rest);
        }
        const auto [gc, r1] = split(gc_1(r0, cp1));
        if (gc.is_integer()) {
            const std::array pair{cp0, gc};
            return b_.cons(b_.list(pair), r1);
        }
        return b_.cons(b_.cons(cp0, gc), r1);
    }

    // zwj (2), another Extend or SpacingMark (1), or neither (0).
    static int extend_kind(const Term &cp) {
        const auto bits = flags(cp);
        return (bits & ZWJ) != 0 ? 2 : (bits & EXTEND) != 0 ? 1 : 0;
    }

    // A cp/1 answer read ahead and the chardata it was read from.
    struct Ahead {
        Term next;
        Term rest;
    };

    // Extend code points after CP0; the tail stays T0 when none follows.
    Term gc_extend(const Ahead &ahead, const Term &cp0) {
        const auto &next = ahead.next;
        const auto &t0 = ahead.rest;
        if (next.is_nil()) {
            return b_.cons(cp0, t0);
        }
        if (next.is_tuple()) {
            return b_.cons(cp0, need(next.tuple_element(1)));
        }
        const auto [cp, t] = split(next);
        if (extend_kind(cp) == 0) {
            return b_.cons(cp0, t0);
        }
        return gc_extend2({.next = this->cp(t), .rest = t}, {cp0, cp});
    }

    // The accumulated cluster (in order) with more Extend code points; after an error, [Cluster, Rest].
    Term gc_extend2(const Ahead &ahead, std::vector<Term> acc) {
        auto next = ahead.next;
        auto t = ahead.rest;
        while (next.is_cons()) {
            const auto [cp, rest] = split(next);
            if (extend_kind(cp) == 0) {
                break;
            }
            acc.push_back(cp);
            t = rest;
            next = this->cp(rest);
        }
        if (next.is_tuple()) {
            const std::array items{b_.list(acc), need(next.tuple_element(1))};
            return b_.list(items);
        }
        return b_.cons(b_.list(acc), t);
    }

    // add_acc: one code point as it is, several as a list.
    Term add_acc(const std::vector<Term> &acc, const Term &rest) {
        return acc.size() == 1 ? b_.cons(acc[0], rest) : b_.cons(b_.list(acc), rest);
    }

    // The cluster at the end of the input ([]), or with an {error, R}'s R appended (OTP's ++ [R], badarg when the
    // result is not a proper list).
    Term ended(const Ahead &ahead, const std::vector<Term> &acc) {
        auto result = add_acc(acc, ahead.rest);
        if (ahead.next.is_nil()) {
            return result;
        }
        std::vector<Term> items;
        Term list = result;
        for (; list.is_cons(); list = list.tail_unchecked()) {
            items.push_back(list.head_unchecked());
        }
        if (!list.is_nil()) {
            bad_argument();
        }
        items.push_back(need(ahead.next.tuple_element(1)));
        return b_.list(items);
    }

    // After a ZWJ in a pictographic sequence: a following pictographic joins (none), else the cluster ends.
    std::optional<Term> zwj_follow(Term &t, std::vector<Term> &acc) {
        const auto next = this->cp(t);
        if (!next.is_cons()) {
            return ended({.next = next, .rest = t}, acc);
        }
        const auto [pict, r2] = split(next);
        if ((flags(pict) & EXT_PICT) == 0) {
            return add_acc(acc, t);
        }
        acc.push_back(pict);
        t = r2;
        return std::nullopt;
    }

    // An extended pictographic sequence: extends, and ZWJ followed by another pictographic.
    Term gc_ext_pict(Term t, std::vector<Term> acc) {
        while (true) {
            const auto next = this->cp(t);
            if (!next.is_cons()) {
                return ended({.next = next, .rest = t}, acc);
            }
            const auto [cp, r1] = split(next);
            const auto kind = extend_kind(cp);
            if (kind == 0) {
                return add_acc(acc, t);
            }
            acc.push_back(cp);
            t = r1;
            if (const auto done = kind == 2 ? zwj_follow(t, acc) : std::nullopt) {
                return *done;
            }
        }
    }

    // Regional indicators pair up.
    Term gc_regional(const Term &r0, const Term &cp0) {
        const auto next = this->cp(r0);
        if (!next.is_cons()) {
            return gc_extend({.next = next, .rest = r0}, cp0);
        }
        const auto [cp, r1] = split(next);
        if ((flags(cp) & REGIONAL) != 0) {
            return gc_extend2({.next = this->cp(r1), .rest = r1}, {cp0, cp});
        }
        return gc_extend({.next = next, .rest = r0}, cp0);
    }

    // Hangul syllables: L* (V+ | LV V* | LVT) T*.
    Term gc_h_l(Term r0, std::vector<Term> acc) {
        while (true) {
            const auto next = this->cp(r0);
            if (!next.is_cons()) {
                return gc_h_lv_lvt(next, r0, acc);
            }
            const auto [cp, r1] = split(next);
            const auto bits = flags(cp);
            if ((bits & (HANGUL_L | HANGUL_V)) == 0) {
                return gc_h_lv_lvt(next, r0, acc);
            }
            acc.push_back(cp);
            r0 = r1;
            if ((bits & HANGUL_V) != 0) {
                return gc_h_v(r0, std::move(acc));
            }
        }
    }

    Term gc_h_v(Term r0, std::vector<Term> acc) {
        while (true) {
            const auto next = this->cp(r0);
            if (!next.is_cons()) {
                return gc_h_end(next, r0, acc);
            }
            const auto [cp, r1] = split(next);
            const auto bits = flags(cp);
            if ((bits & (HANGUL_V | HANGUL_T)) == 0) {
                return gc_h_end(next, r0, acc);
            }
            acc.push_back(cp);
            r0 = r1;
            if ((bits & HANGUL_T) != 0) {
                return gc_h_t(r0, std::move(acc));
            }
        }
    }

    Term gc_h_t(Term r0, std::vector<Term> acc) {
        while (true) {
            const auto next = this->cp(r0);
            if (!next.is_cons()) {
                return gc_h_end(next, r0, acc);
            }
            const auto [cp, r1] = split(next);
            if ((flags(cp) & HANGUL_T) == 0) {
                return gc_h_end(next, r0, acc);
            }
            acc.push_back(cp);
            r0 = r1;
        }
    }

    Term gc_h_end(const Term &r1, const Term &r0, const std::vector<Term> &acc) {
        const Ahead ahead{.next = r1, .rest = r0};
        return acc.size() == 1 ? gc_extend(ahead, acc[0]) : gc_extend2(ahead, acc);
    }

    Term gc_h_lv_lvt(const Term &r, const Term &r0, const std::vector<Term> &acc) {
        if (!r.is_cons()) {
            return gc_h_end(r, r0, acc);
        }
        const auto [cp, r1] = split(r);
        if (!is_cp(cp)) {
            bad_argument();
        }
        const auto bits = flags(cp);
        if ((bits & (HANGUL_LV | HANGUL_LVT)) != 0) {
            auto more = acc;
            more.push_back(cp);
            return (bits & HANGUL_LV) != 0 ? gc_h_v(r1, std::move(more)) : gc_h_t(r1, std::move(more));
        }
        return acc.empty() ? gc_extend({.next = this->cp(r1), .rest = r1}, cp) : gc_h_end(r, r0, acc);
    }

    // Indic conjuncts: consonants joined by linkers, with extends between them.
    Term gc_indic(Term next, Term r0, bool linked, std::vector<Term> cps) {
        while (next.is_cons()) {
            const auto [cp, r1] = split(next);
            const auto bits = flags(cp);
            if ((bits & LINKER) != 0) {
                linked = true;
            } else if (extend_kind(cp) == 0) {
                if (!linked || (bits & CONSONANT) == 0) {
                    return add_acc(cps, r0);
                }
                linked = false;
            }
            cps.push_back(cp);
            r0 = r1;
            next = this->cp(r1);
        }
        return add_acc(cps, next.is_nil() ? r0 : need(next.tuple_element(1)));
    }

    ProcessContext &context_;
    Build b_;
    CodePoints cps_;
};
} // namespace

Term cp_term(ProcessContext &context, const Term &data) { return CodePoints(context).cp(data); }

Term gc_term(ProcessContext &context, const Term &data) { return Clusters(context).gc(data); }

Next split(const Term &result) { return {.item = need(result.head()), .rest = need(result.tail())}; }

std::uint32_t flags_of(char32_t code) { return tables::lookup(tables::flag_ranges(), code, 0); }

std::pair<std::string_view, std::string_view> category_names(char32_t code) {
    return tables::CATEGORIES.at(tables::lookup(tables::category_ranges(), code, tables::NOT_ASSIGNED));
}

Term category_term(ProcessContext &context, char32_t code) {
    const auto [group, name] = category_names(code);
    TermFactory factory(context);
    const std::array parts{need(factory.atom(group)), need(factory.atom(name))};
    return need(factory.tuple(parts));
}

Term mapped_first(ProcessContext &context, const Term &data, Case which) {
    auto next = cp_term(context, data);
    if (next.is_nil()) {
        return next;
    }
    if (next.is_tuple()) {
        TermFactory factory(context);
        const std::array reason{need(factory.atom("badarg")), need(next.tuple_element(1))};
        throw BuiltinFailure{.reason = abi::v1::ErrorReason::raised_error,
                             .payload = need(factory.tuple(reason)).word()};
    }
    const auto [cp, rest] = split(next);
    const auto mapped = case_term(context, static_cast<char32_t>(*small_of(cp)), which);
    return need(TermFactory(context).cons(mapped, rest));
}

Term case_term(ProcessContext &context, char32_t code, Case which) {
    const auto entries = tables::cases();
    const auto found = std::ranges::lower_bound(entries, code, {}, &tables::CaseMapping::code_point);
    TermFactory factory(context);
    if (found == entries.end() || found->code_point != code) {
        return need(factory.integer(static_cast<std::int64_t>(code)));
    }
    const auto run = tables::points().subspan(found->offsets.at(static_cast<std::size_t>(which)),
                                              found->sizes.at(static_cast<std::size_t>(which)));
    if (run.size() == 1) {
        return need(factory.integer(static_cast<std::int64_t>(run[0])));
    }
    std::vector<Term> codes;
    for (const auto point : run) {
        codes.push_back(need(factory.integer(static_cast<std::int64_t>(point))));
    }
    return need(factory.list(codes));
}
} // namespace clause::runtime::builtins::unicode
