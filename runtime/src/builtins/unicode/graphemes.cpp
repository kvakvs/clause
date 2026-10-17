#include "graphemes.hpp"
#include "tables.hpp"
#include <array>
#include <cstdint>
#include <optional>

// unicode_util:gc/1's cluster rules on a flat code point sequence (plan 21 step LIB5G): CR LF, controls, prepended
// code points, Hangul syllable sequences, regional indicator pairs, extended pictographic sequences joined by ZWJ,
// Indic conjuncts and trailing Extend, ZWJ and SpacingMark code points.
namespace clause::runtime::builtins::unicode {
namespace {
// unicode_util.erl's flag bits.
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

// A cursor over the code points after a cluster's first.
class Cluster final {
  public:
    Cluster(std::u32string_view codes, std::size_t at) : codes_(codes), start_(at), end_(at + 1) {}

    std::size_t length() const { return end_ - start_; }

    bool has(std::uint32_t flag, std::size_t offset = 0) const {
        return end_ + offset < codes_.size() && (flags(codes_[end_ + offset]) & flag) != 0;
    }

    void take(std::size_t count = 1) { end_ += count; }

    // Extend, ZWJ and SpacingMark code points.
    std::size_t extended() {
        while (has(EXTEND | ZWJ)) {
            take();
        }
        return length();
    }

    std::size_t pictographic() {
        while (true) {
            if (has(ZWJ)) {
                take();
                if (!has(EXT_PICT)) {
                    return length();
                }
                take();
            } else if (has(EXTEND)) {
                take();
            } else {
                return length();
            }
        }
    }

    // L* (V+ | LV V* | LVT) T* after an L, V or T, or from an LV or LVT syllable.
    std::size_t hangul(std::uint32_t state) {
        while (const auto next = hangul_step(state)) {
            state = *next;
            take();
        }
        return extended();
    }

    // The Hangul state after the next code point, or none when it ends the syllable.
    std::optional<std::uint32_t> hangul_step(std::uint32_t state) const {
        struct Step {
            std::uint32_t from;
            std::uint32_t on;
            std::uint32_t to;
        };

        constexpr std::array<Step, 6> STEPS{{{HANGUL_L, HANGUL_L, HANGUL_L},
                                             {HANGUL_L, HANGUL_V | HANGUL_LV, HANGUL_V},
                                             {HANGUL_L, HANGUL_LVT, HANGUL_T},
                                             {HANGUL_V, HANGUL_V, HANGUL_V},
                                             {HANGUL_V, HANGUL_T, HANGUL_T},
                                             {HANGUL_T, HANGUL_T, HANGUL_T}}};
        for (const auto &step : STEPS) {
            if (state == step.from && has(step.on)) {
                return step.to;
            }
        }
        return std::nullopt;
    }

    // Consonants joined by linkers, extends between them.
    std::size_t indic() {
        bool linked = false;
        while (true) {
            if (has(LINKER)) {
                linked = true;
                take();
            } else if (has(EXTEND | ZWJ)) {
                take();
            } else if (linked && has(CONSONANT)) {
                linked = false;
                take();
            } else {
                return length();
            }
        }
    }

    static std::uint32_t flags(char32_t code) { return unicode_tables::lookup(unicode_tables::flag_ranges(), code, 0); }

  private:
    std::u32string_view codes_;
    std::size_t start_;
    std::size_t end_;
};

bool latin1(char32_t code) { return code <= 255; }

std::size_t classified(std::u32string_view codes, std::size_t at);

// A prepended code point joins the cluster after it unless a control follows.
std::size_t prepended(std::u32string_view codes, std::size_t at) {
    if (at + 1 >= codes.size() || (Cluster::flags(codes[at + 1]) & CONTROL) != 0) {
        return 1;
    }
    return 1 + grapheme_length(codes, at + 1);
}

std::size_t syllable(Cluster cluster, std::uint32_t flags) {
    if ((flags & HANGUL_LV) != 0) {
        return cluster.hangul(HANGUL_V);
    }
    if ((flags & HANGUL_LVT) != 0) {
        return cluster.hangul(HANGUL_T);
    }
    return cluster.extended();
}

std::size_t classified(std::u32string_view codes, std::size_t at) {
    const auto flags = Cluster::flags(codes[at]);
    Cluster cluster(codes, at);
    if ((flags & CONTROL) != 0) {
        return 1;
    }
    if ((flags & PREPEND) != 0) {
        return prepended(codes, at);
    }
    if ((flags & (HANGUL_L | HANGUL_V | HANGUL_T)) != 0) {
        return cluster.hangul(flags & (HANGUL_L | HANGUL_V | HANGUL_T));
    }
    if (codes[at] >= 44000 && codes[at] <= 56000) {
        return syllable(cluster, flags);
    }
    if ((flags & REGIONAL) != 0 && cluster.has(REGIONAL)) {
        cluster.take();
        return cluster.extended();
    }
    if ((flags & EXT_PICT) != 0) {
        return cluster.pictographic();
    }
    return (flags & CONSONANT) != 0 ? cluster.indic() : cluster.extended();
}

// A Latin-1 code point not followed by one: controls stand alone, pictographic ones take their sequence.
std::size_t latin1_cluster(std::u32string_view codes, std::size_t at) {
    const auto flags = Cluster::flags(codes[at]);
    Cluster cluster(codes, at);
    if ((flags & CONTROL) != 0) {
        return 1;
    }
    return (flags & EXT_PICT) != 0 ? cluster.pictographic() : cluster.extended();
}
} // namespace

std::size_t grapheme_length(std::u32string_view codes, std::size_t at) {
    const auto first = codes[at];
    const bool next = at + 1 < codes.size();
    if (first == '\r') {
        return next && codes[at + 1] == '\n' ? 2 : 1;
    }
    if (latin1(first)) {
        return next && latin1(codes[at + 1]) ? 1 : latin1_cluster(codes, at);
    }
    return classified(codes, at);
}
} // namespace clause::runtime::builtins::unicode
