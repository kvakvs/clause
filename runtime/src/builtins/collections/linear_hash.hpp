#pragma once
#include "collections.hpp"
#include <map>
#include <vector>

// OTP's linear hash tables (plan 21 step LIB2, docs/builtins.md#collections): dict and sets of version 1 keep their
// entries in OTP's record {Tag, Size, N, MaxN, Bso, ExpSize, ConSize, Empty, Segs}, segments of 16 bucket lists, and
// slots chosen by erlang:phash/2. The table changes them exactly as OTP's maybe_expand and maybe_contract do, so the
// records equal OTP's, and lists and folds follow OTP's order.
namespace clause::runtime::builtins::collections {
class LinearHash final {
  public:
    // Which record: dict buckets hold [Key | Value] cells, set buckets the elements.
    enum class Kind : std::uint8_t { dict, set };

    // The table of `record`; {badrecord, Record} unless it is such a record.
    LinearHash(ProcessContext &context, const Term &record, Kind kind);

    // Whether `term` is a record of the kind: a 9-tuple with the tag first.
    static bool is_record(const Term &term, Kind kind);

    // An empty table's record, as new/0 makes it.
    static Term fresh(ProcessContext &context, Kind kind);

    // The number of entries.
    std::size_t size() const { return static_cast<std::size_t>(size_); }

    // The slot (from 1) of `key`.
    std::size_t slot(const Term &key) const;

    // The elements of the bucket of `slot`, and replace them.
    std::vector<Term> bucket(std::size_t slot);
    void put(std::size_t slot, std::span<const Term> elements);

    // OTP's maybe_expand after `added` (0 or 1) entries came and maybe_contract after `removed` went.
    void grow(std::size_t added);
    void shrink(std::size_t removed);

    // The record with the changes.
    Term record();

    // Every entry in storage order (segments, buckets and bucket elements in order) and in fold order (segments and
    // buckets from the last, bucket elements in order).
    std::vector<Term> entries() const;
    std::vector<Term> fold_order() const;

    // The key of an entry: a dict cell's head, a set element itself.
    Term key_of(const Term &entry) const;

  private:
    // The decoded buckets of segment `index` (from 0), decoded on first use.
    std::vector<Term> &segment(std::size_t index);
    // The buckets of segment `index` as they are now, decoded or not.
    std::vector<Term> buckets(std::size_t index) const;

    ProcessContext *context_;
    Kind kind_;
    // The record's fields: entry count, active slots, slots of the segments, buddy slot offset, and the sizes at
    // which the table expands and contracts.
    std::int64_t size_ = 0;
    std::int64_t n_ = 0;
    std::int64_t maxn_ = 0;
    std::int64_t bso_ = 0;
    std::int64_t exp_size_ = 0;
    std::int64_t con_size_ = 0;
    // The empty segment new segments start as, and the segment tuples.
    Term empty_;
    std::vector<Term> segs_;
    // Segments changed so far, by index, as bucket lists.
    std::map<std::size_t, std::vector<Term>> decoded_;
};
} // namespace clause::runtime::builtins::collections
