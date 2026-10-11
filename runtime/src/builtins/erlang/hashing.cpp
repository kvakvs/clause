#include "../../process/identities.hpp"
#include "../../terms/funs.hpp"
#include "../../terms/identities.hpp"
#include "../../terms/integers.hpp"
#include "../../terms/structural_order.hpp"
#include "../typed.hpp"
#include "terms.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <clause/runtime/atoms.hpp>
#include <vector>

// phash/2 and phash2/1,2 (plan step 41G3, docs/builtins.md#hashing): a hash of a term's structure that is the same
// for exactly equal terms in every program run (atoms hash by spelling, maps in key order). The values are Clause's
// own, not OTP's.
namespace clause::runtime::builtins {
namespace {
// FNV-1a over 64-bit words with a final avalanche.
class TermHash {
  public:
    // Hash `root` iteratively.
    std::uint64_t of(ProcessContext &context, const Term &root) {
        std::vector<Term> pending{root};
        while (!pending.empty()) {
            const auto term = pending.back();
            pending.pop_back();
            one(context, term, pending);
        }
        auto value = state_;
        value ^= value >> 33U;
        value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33U;
        return value;
    }

  private:
    // Add one word.
    void mix(std::uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            state_ ^= (value >> (8 * i)) & 0xFFU;
            state_ *= 0x100000001b3ULL;
        }
    }

    // Add a kind tag and bytes.
    void bytes(std::uint64_t kind, std::span<const std::byte> data) {
        mix(kind);
        mix(data.size());
        for (const auto byte : data) {
            state_ ^= std::to_integer<std::uint64_t>(byte);
            state_ *= 0x100000001b3ULL;
        }
    }

    void text(std::uint64_t kind, std::string_view data) { bytes(kind, std::as_bytes(std::span(data))); }

    // Add one term, pushing its children to be hashed next.
    void one(ProcessContext &context, const Term &term, std::vector<Term> &pending) {
        if (term.is_integer()) {
            text(1, need(term.integer_decimal()));
        } else if (term.is_float()) {
            mix(2);
            mix(std::bit_cast<std::uint64_t>(need(term.float_value())));
        } else if (term.is_atom()) {
            text(3, need(term.atom_spelling()));
        } else if (term.is_bitstring()) {
            mix(need(term.bit_size()));
            bytes(4, need(term.bitstring_bytes()));
        } else {
            container(context, term, pending);
        }
    }

    // Lists, tuples, maps, native records, identities and funs.
    void container(ProcessContext &context, const Term &term, std::vector<Term> &pending) {
        if (term.is_nil()) {
            mix(5);
        } else if (term.is_cons()) {
            mix(6);
            pending.push_back(need(term.tail()));
            pending.push_back(need(term.head()));
        } else if (term.is_tuple()) {
            children(7, need(term.tuple_elements()), pending);
        } else if (term.is_map()) {
            map(term, pending);
        } else {
            identity(context, term, pending);
        }
    }

    void children(std::uint64_t kind, const std::vector<Term> &items, std::vector<Term> &pending) {
        mix(kind);
        mix(items.size());
        pending.insert(pending.end(), items.rbegin(), items.rend());
    }

    // A map's pairs in key order, so equal maps hash equally whatever their layout.
    void map(const Term &term, std::vector<Term> &pending) {
        auto entries = need(term.map_entries());
        std::ranges::sort(entries, [](const auto &left, const auto &right) {
            return need(detail::structural_order(left.first, right.first, true)) < 0;
        });
        mix(8);
        mix(entries.size());
        for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
            pending.push_back(entry->second);
            pending.push_back(entry->first);
        }
    }

    // A pid or port by its number.
    void process_or_port(const Term &term) {
        if (term.is_pid()) {
            mix(9);
            mix(detail::pid_number(term.word()));
        } else {
            mix(10);
            mix(detail::port_number(term.word()));
        }
    }

    void identity(ProcessContext &context, const Term &term, std::vector<Term> &pending) {
        if (term.is_pid() || term.is_port()) {
            process_or_port(term);
        } else if (term.is_reference()) {
            mix(11);
            mix(need(detail::reference_number(term)));
        } else if (term.is_function()) {
            fun(context, term, pending);
        } else {
            mix(12);
            for (const auto &[name, value] : need(term.record_fields())) {
                pending.push_back(value);
                pending.push_back(name);
            }
        }
    }

    // A fun by module, name, arity and captured values.
    void fun(ProcessContext &context, const Term &term, std::vector<Term> &pending) {
        const auto view = need(detail::fun_view(term));
        const auto &atoms = context.atom_storage();
        mix(13);
        text(3, need(need(atoms.lookup(view.definition->module)).atom_spelling()));
        text(3, need(need(atoms.lookup(view.definition->function)).atom_spelling()));
        mix(view.definition->arity);
        for (const auto word : view.captures) {
            pending.push_back(need(Term::from_word(word, context)));
        }
    }

    std::uint64_t state_ = 0xcbf29ce484222325ULL;
};

std::uint64_t hash(ProcessContext &context, const Term &term) { return TermHash{}.of(context, term); }

// A range argument: an integer in 1..2^32; badarg otherwise.
std::uint64_t range_of(const detail::Integer &range) {
    if (range < 1 || range > (detail::Integer(1) << 32)) {
        bad_argument();
    }
    return range.convert_to<std::uint64_t>();
}

TermResult<Term> phash2_1(ProcessContext &context, const Term &term) {
    return TermFactory(context).integer(static_cast<std::int64_t>(hash(context, term) % (std::uint64_t{1} << 27)));
}

TermResult<Term> phash2_2(ProcessContext &context, const Term &term, const detail::Integer &range) {
    const auto limit = range_of(range);
    return TermFactory(context).integer(static_cast<std::int64_t>(hash(context, term) % limit));
}

// phash/2: 1..Range.
TermResult<Term> phash(ProcessContext &context, const Term &term, const detail::Integer &range) {
    const auto limit = range_of(range);
    return TermFactory(context).integer(static_cast<std::int64_t>(hash(context, term) % limit + 1));
}

constexpr std::array HASHING_BUILTINS{
    typed_entry<phash2_1>("erlang", "phash2"),
    typed_entry<phash2_2>("erlang", "phash2"),
    typed_entry<phash>("erlang", "phash"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> hashing_builtins() noexcept { return builtins::HASHING_BUILTINS; }
} // namespace clause::runtime
