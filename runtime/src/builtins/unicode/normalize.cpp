#include "normalize.hpp"
#include "code_points.hpp"
#include "tables.hpp"
#include <algorithm>
#include <array>
#include <vector>

// unicode_util's nfd/nfkd/nfc/nfkc (plan 21 step LIB4): the cluster gc/1 finds, decomposed through the tables
// (Hangul syllables to jamo), canonically ordered, and for the composed forms recombined pairwise as OTP's
// compose/compose_many do. Clusters are handled as code point lists; a single code point is returned bare.
namespace clause::runtime::builtins::unicode {
namespace {
namespace tables = unicode_tables;

// A decomposed code point with its combining class.
struct Tagged {
    std::uint32_t ccc;
    char32_t code;
};

using Codes = std::vector<char32_t>;

bool is_hangul(char32_t code) { return code >= 0xAC00 && code <= 0xD7A3; }

std::uint32_t combining_class(char32_t code) { return tables::lookup(tables::class_ranges(), code, 0); }

const tables::Decomposition *decomposition(char32_t code) {
    const auto entries = tables::decompositions();
    const auto found = std::ranges::lower_bound(entries, code, {}, &tables::Decomposition::code_point);
    return found != entries.end() && found->code_point == code ? &*found : nullptr;
}

std::vector<Tagged> tagged_run(std::uint32_t offset, std::uint8_t size) {
    std::vector<Tagged> run;
    for (const auto code : tables::points().subspan(offset, size)) {
        run.push_back({.ccc = combining_class(code), .code = code});
    }
    return run;
}

// The jamo of a precomposed Hangul syllable.
std::vector<Tagged> hangul_jamo(char32_t code) {
    const auto syllable = code - 0xAC00;
    std::vector<Tagged> jamo{{0, 0x1100 + syllable / 588}, {0, 0x1161 + (syllable % 588) / 28}};
    if (syllable % 28 != 0) {
        jamo.push_back({0, syllable % 28 + 0x11A7});
    }
    return jamo;
}

// decompose_1 / decompose_compat_1 of one code point: [{CCC, CP}].
std::vector<Tagged> decomposed_point(char32_t code, bool compat) {
    if (is_hangul(code)) {
        return hangul_jamo(code);
    }
    const auto *entry = decomposition(code);
    if (entry != nullptr && compat && entry->compat_tag != 0) {
        return tagged_run(entry->compat, entry->compat_size);
    }
    if (entry != nullptr && entry->canon_size != 0) {
        return tagged_run(entry->canon, entry->canon_size);
    }
    return {{.ccc = combining_class(code), .code = code}};
}

// canonical_order: runs of non-starters sorted by combining class (stable).
Codes canonical_order(std::vector<Tagged> tagged) {
    Codes codes;
    for (std::size_t at = 0; at < tagged.size();) {
        if (tagged[at].ccc == 0) {
            codes.push_back(tagged[at++].code);
            continue;
        }
        auto end = at;
        while (end < tagged.size() && tagged[end].ccc > 0) {
            ++end;
        }
        std::stable_sort(tagged.begin() + static_cast<std::ptrdiff_t>(at),
                         tagged.begin() + static_cast<std::ptrdiff_t>(end),
                         [](const Tagged &a, const Tagged &b) { return a.ccc < b.ccc; });
        for (; at < end; ++at) {
            codes.push_back(tagged[at].code);
        }
    }
    return codes;
}

// decompose / decompose_compat of a cluster (one code point or several).
Codes decomposed(const Codes &cluster, bool compat) {
    std::vector<Tagged> tagged;
    for (const auto code : cluster) {
        const auto part = decomposed_point(code, compat);
        tagged.insert(tagged.end(), part.begin(), part.end());
    }
    return canonical_order(std::move(tagged));
}

std::optional<char32_t> compose_pair(char32_t first, char32_t second) {
    const tables::Composition key{first, second, 0};
    const auto entries = tables::compositions();
    const auto order = [](const tables::Composition &left, const tables::Composition &right) {
        return std::pair(left.first, left.second) < std::pair(right.first, right.second);
    };
    const auto found = std::ranges::lower_bound(entries, key, order);
    if (found == entries.end() || found->first != key.first || found->second != key.second) {
        return std::nullopt;
    }
    return found->composed;
}

Codes compose(const Codes &codes);

// compose_hangul: a leading syllable from its jamo, or the lead kept before the composed rest.
Codes compose_hangul(const Codes &codes) {
    const auto lead = codes[0];
    const auto vowel = codes[1];
    Codes trail(codes.begin() + 2, codes.end());
    if (vowel >= 0x1161 && vowel <= 0x1175) {
        auto syllable = 0xAC00 + (lead - 0x1100) * 588 + (vowel - 0x1161) * 28;
        if (!trail.empty() && trail[0] >= 0x11A7 && trail[0] <= 0x11C2) {
            syllable += trail[0] - 0x11A7;
            trail.erase(trail.begin());
        }
        trail.insert(trail.begin(), syllable);
        return trail;
    }
    Codes rest(codes.begin() + 1, codes.end());
    auto composed = compose(rest);
    composed.insert(composed.begin(), lead);
    return composed;
}

// compose_many: accents combine with the base when their class allows; a starter that does not combine begins a new
// base.
Codes compose_many(const Codes &codes) {
    Codes out;
    char32_t base = codes[0];
    Codes accents;
    std::uint32_t previous = combining_class(base);
    for (std::size_t at = 1; at < codes.size(); ++at) {
        const auto code = codes[at];
        const auto klass = combining_class(code);
        const auto combined = previous == 0 || previous < klass ? compose_pair(base, code) : std::nullopt;
        if (combined) {
            base = *combined;
        } else if (klass == 0) {
            out.push_back(base);
            out.insert(out.end(), accents.begin(), accents.end());
            accents.clear();
            base = code;
            previous = 0;
        } else {
            accents.push_back(code);
            previous = klass;
        }
    }
    out.push_back(base);
    out.insert(out.end(), accents.begin(), accents.end());
    return out;
}

Codes compose(const Codes &codes) {
    if (codes.size() <= 1) {
        return codes;
    }
    if (codes[0] >= 0x1100 && codes[0] <= 0x1112) {
        return compose_hangul(codes);
    }
    if (codes.size() == 2) {
        const auto pair = compose_pair(codes[0], codes[1]);
        return pair ? Codes{*pair} : codes;
    }
    return compose_many(codes);
}

// The code points of a cluster term (an integer or a list of them).
Codes cluster_codes(const Term &cluster) {
    Codes codes;
    if (cluster.is_integer()) {
        codes.push_back(static_cast<char32_t>(*cluster.integer_value()));
        return codes;
    }
    for (Term list = cluster; list.is_cons(); list = list.tail_unchecked()) {
        codes.push_back(static_cast<char32_t>(*list.head_unchecked().integer_value()));
    }
    return codes;
}

// A code point list as OTP's cluster term: one code point bare.
Term cluster_term(ProcessContext &context, const Codes &codes) {
    TermFactory factory(context);
    if (codes.size() == 1) {
        return need(factory.integer(static_cast<std::int64_t>(codes[0])));
    }
    std::vector<Term> items;
    for (const auto code : codes) {
        items.push_back(need(factory.integer(static_cast<std::int64_t>(code))));
    }
    return need(factory.list(items));
}

// compose_compat_0: the canonical composition of every cluster of the decomposition.
Codes compose_compat(ProcessContext &context, const Codes &codes) {
    if (codes.size() <= 1) {
        return codes;
    }
    Codes out;
    Term rest = cluster_term(context, codes);
    while (true) {
        const auto next = gc_term(context, rest);
        if (!next.is_cons()) {
            return out;
        }
        const auto [cluster, more] = split(next);
        const auto composed = compose(cluster_codes(cluster));
        out.insert(out.end(), composed.begin(), composed.end());
        rest = more;
    }
}

// The normalized cluster as OTP's term: one code point bare, except that nfkc keeps a list for a cluster whose
// decomposition has several code points (compose_compat_0 builds a list there).
Term normalized_cluster(ProcessContext &context, const Codes &cluster, Form form) {
    switch (form) {
    case Form::nfd:
        return cluster_term(context, decomposed(cluster, false));
    case Form::nfkd:
        return cluster_term(context, decomposed(cluster, true));
    case Form::nfc:
        return cluster_term(context, compose(decomposed(cluster, false)));
    case Form::nfkc:
        break;
    }
    const auto codes = decomposed(cluster, true);
    if (codes.size() <= 1) {
        return cluster_term(context, codes);
    }
    std::vector<Term> items;
    for (const auto code : compose_compat(context, codes)) {
        items.push_back(need(TermFactory(context).integer(static_cast<std::int64_t>(code))));
    }
    return need(TermFactory(context).list(items));
}

// The clusters each form leaves as they are: ASCII, and for nfc all of Latin-1.
bool unchanged(const Term &cluster, Form form) {
    const auto code = cluster.is_integer() ? cluster.integer_value() : std::unexpected(TermError::wrong_type);
    return code && *code >= 0 && *code <= (form == Form::nfc ? 255 : 127);
}
} // namespace

Term normalized_term(ProcessContext &context, const Term &data, Form form) {
    auto next = gc_term(context, data);
    if (!next.is_cons()) {
        return next;
    }
    const auto [cluster, rest] = split(next);
    if (unchanged(cluster, form)) {
        return next;
    }
    return need(TermFactory(context).cons(normalized_cluster(context, cluster_codes(cluster), form), rest));
}
} // namespace clause::runtime::builtins::unicode
