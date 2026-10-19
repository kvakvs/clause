#pragma once
#include "format.hpp"
#include <terms.hpp>

// The terms of dets tables (plan 21 step LIB53): objects stored as their external format, keys hashed by phash2
// (or phash for old files) and ordered by dets_utils:cmp/2, Erlang's term order with integers before equal floats.
namespace clause::runtime::builtins::dets {
// Bytes that hold no object; the caller reports the collection as a bad object.
struct BadObject {};

class Terms final {
  public:
    explicit Terms(ProcessContext &context) : context_(context) {}

    ProcessContext &context() const { return context_; }

    // The term an object's bytes encode; throws BadObject.
    Term decode(std::span<const std::byte> bytes) const;
    Bytes encode(const Term &term) const;
    // The hash of a key: phash2's 27 bits, or phash(Key, 2^26-1) - 1 for files of the old method.
    std::uint32_t hash(const Term &key, HashMethod method) const;
    // Element `position` (1-based) of an object; throws BadObject when it has none.
    static Term element(const Term &object, std::uint32_t position);
    // dets_utils:cmp/2: 0 for the same term, else -1 or 1; integers sort before floats of equal value.
    static int cmp(const Term &left, const Term &right);
    static bool same(const Term &left, const Term &right);

    TermFactory factory() const { return TermFactory(context_); }

  private:
    ProcessContext &context_;
};
} // namespace clause::runtime::builtins::dets
