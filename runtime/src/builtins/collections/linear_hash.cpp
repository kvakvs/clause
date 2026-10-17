#include "linear_hash.hpp"
#include "../erlang/hashing.hpp"
#include <array>

// OTP's linear hash tables of dict and sets version 1 (plan 21 step LIB2).
namespace clause::runtime::builtins::collections {
namespace {
// Buckets per segment, and the loads per slot at which a table expands and contracts.
constexpr std::size_t SEGMENT = 16;
constexpr std::int64_t EXPAND_LOAD = 5;
constexpr std::int64_t CONTRACT_LOAD = 3;
// The record's fields: tag, size, n, maxn, bso, exp_size, con_size, empty, segs.
constexpr std::size_t FIELDS = 9;

std::string_view tag_of(LinearHash::Kind kind) { return kind == LinearHash::Kind::dict ? "dict" : "set"; }

[[noreturn]] void bad_record(const Term &term) {
    throw BuiltinFailure{.reason = abi::v1::ErrorReason::badrecord, .payload = term.word()};
}

// The elements of a bucket list.
std::vector<Term> elements(const Term &list) { return lists::proper(list); }

Term tuple_of(ProcessContext &context, std::span<const Term> parts) { return need(TermFactory(context).tuple(parts)); }
} // namespace

bool LinearHash::is_record(const Term &term, Kind kind) {
    if (!term.is_tuple() || term.tuple_size_unchecked() != FIELDS) {
        return false;
    }
    const auto tag = term.tuple_element_unchecked(0);
    return tag.is_atom() && tag.atom_spelling_unchecked() == tag_of(kind);
}

LinearHash::LinearHash(ProcessContext &context, const Term &record, Kind kind) : context_(&context), kind_(kind) {
    if (!is_record(record, kind)) {
        bad_record(record);
    }
    const auto parts = need(record.tuple_elements());
    // A record field that must be a small integer.
    const auto field = [&](const Term &value) {
        const auto number = small(value.word());
        if (!number) {
            bad_record(record);
        }
        return *number;
    };
    size_ = field(parts[1]);
    n_ = field(parts[2]);
    maxn_ = field(parts[3]);
    bso_ = field(parts[4]);
    exp_size_ = field(parts[5]);
    con_size_ = field(parts[6]);
    empty_ = parts[7];
    if (!parts[8].is_tuple() || maxn_ <= 0) {
        bad_record(record);
    }
    segs_ = need(parts[8].tuple_elements());
}

Term LinearHash::fresh(ProcessContext &context, Kind kind) {
    TermFactory factory(context);
    const std::vector<Term> nils(SEGMENT, need(factory.nil()));
    const auto empty = tuple_of(context, nils);
    const auto integer = [&](std::int64_t value) { return need(factory.integer(value)); };
    const std::array parts{need(factory.atom(tag_of(kind))),
                           integer(0),
                           integer(SEGMENT),
                           integer(SEGMENT),
                           integer(SEGMENT / 2),
                           integer(SEGMENT * EXPAND_LOAD),
                           integer(SEGMENT * CONTRACT_LOAD),
                           empty,
                           tuple_of(context, std::array{empty})};
    return tuple_of(context, parts);
}

std::size_t LinearHash::slot(const Term &key) const {
    const auto hash = make_hash(*context_, key);
    auto slot = static_cast<std::int64_t>(1 + hash % static_cast<std::uint64_t>(maxn_));
    if (slot > n_) {
        slot -= bso_;
    }
    return static_cast<std::size_t>(slot);
}

std::vector<Term> &LinearHash::segment(std::size_t index) {
    auto found = decoded_.find(index);
    if (found == decoded_.end()) {
        found = decoded_.emplace(index, need(segs_.at(index).tuple_elements())).first;
    }
    return found->second;
}

std::vector<Term> LinearHash::buckets(std::size_t index) const {
    const auto found = decoded_.find(index);
    return found != decoded_.end() ? found->second : need(segs_.at(index).tuple_elements());
}

std::vector<Term> LinearHash::bucket(std::size_t slot) {
    return elements(segment((slot - 1) / SEGMENT).at((slot - 1) % SEGMENT));
}

void LinearHash::put(std::size_t slot, std::span<const Term> entries) {
    segment((slot - 1) / SEGMENT).at((slot - 1) % SEGMENT) = need(TermFactory(*context_).list(entries));
}

Term LinearHash::key_of(const Term &entry) const { return kind_ == Kind::dict ? need(entry.head()) : entry; }

void LinearHash::grow(std::size_t added) {
    const auto total = size_ + static_cast<std::int64_t>(added);
    if (total <= exp_size_) {
        size_ = total;
        return;
    }
    if (n_ == maxn_) {
        maxn_ *= 2;
        bso_ *= 2;
        segs_.insert(segs_.end(), segs_.size(), empty_);
    }
    const auto next = n_ + 1;
    const auto low = static_cast<std::size_t>(next - bso_);
    std::vector<Term> stay;
    std::vector<Term> move;
    for (const auto &entry : bucket(low)) {
        const auto home = 1 + make_hash(*context_, key_of(entry)) % static_cast<std::uint64_t>(maxn_);
        (home == low ? stay : move).push_back(entry);
    }
    put(low, stay);
    put(static_cast<std::size_t>(next), move);
    size_ = total;
    n_ = next;
    exp_size_ = next * EXPAND_LOAD;
    con_size_ = next * CONTRACT_LOAD;
}

void LinearHash::shrink(std::size_t removed) {
    const auto total = size_ - static_cast<std::int64_t>(removed);
    if (total >= con_size_ || n_ <= static_cast<std::int64_t>(SEGMENT)) {
        size_ = total;
        return;
    }
    const auto high = static_cast<std::size_t>(n_);
    const auto low = static_cast<std::size_t>(n_ - bso_);
    auto merged = bucket(low);
    const auto upper = bucket(high);
    merged.insert(merged.end(), upper.begin(), upper.end());
    put(low, merged);
    put(high, {});
    size_ = total;
    n_ -= 1;
    exp_size_ = n_ * EXPAND_LOAD;
    con_size_ = n_ * CONTRACT_LOAD;
    if (n_ == bso_) {
        maxn_ /= 2;
        bso_ /= 2;
        segs_.resize(segs_.size() / 2);
        std::erase_if(decoded_, [&](const auto &entry) { return entry.first >= segs_.size(); });
    }
}

Term LinearHash::record() {
    TermFactory factory(*context_);
    for (const auto &[index, buckets] : decoded_) {
        segs_[index] = tuple_of(*context_, buckets);
    }
    decoded_.clear();
    const auto integer = [&](std::int64_t value) { return need(factory.integer(value)); };
    const std::array parts{need(factory.atom(tag_of(kind_))),
                           integer(size_),
                           integer(n_),
                           integer(maxn_),
                           integer(bso_),
                           integer(exp_size_),
                           integer(con_size_),
                           empty_,
                           tuple_of(*context_, segs_)};
    return tuple_of(*context_, parts);
}

std::vector<Term> LinearHash::entries() const {
    std::vector<Term> result;
    for (std::size_t index = 0; index < segs_.size(); ++index) {
        for (const auto &list : buckets(index)) {
            const auto items = elements(list);
            result.insert(result.end(), items.begin(), items.end());
        }
    }
    return result;
}

std::vector<Term> LinearHash::fold_order() const {
    std::vector<Term> result;
    for (auto index = segs_.size(); index-- > 0;) {
        const auto lists = buckets(index);
        for (auto list = lists.rbegin(); list != lists.rend(); ++list) {
            const auto items = elements(*list);
            result.insert(result.end(), items.begin(), items.end());
        }
    }
    return result;
}
} // namespace clause::runtime::builtins::collections
