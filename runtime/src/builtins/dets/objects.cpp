#include "objects.hpp"
#include "../collections/collections.hpp"
#include "../erlang/hashing.hpp"
#include "../erlang/term_decoding.hpp"
#include "../erlang/term_encoding.hpp"

namespace clause::runtime::builtins::dets {
namespace {
// The hash range of the old phash method; dets_v9's ?BIG.
constexpr std::uint32_t BIG = 0x3ffffff;
constexpr std::uint32_t PHASH2_MASK = (std::uint32_t{1} << 27U) - 1;

int ordered(const Term &left, const Term &right) { return collections::compare(left, right) < 0 ? -1 : 1; }

// An integer sorts before a float, a float after an integer; none for other pairs.
std::optional<int> cmp_numbers(const Term &left, const Term &right) {
    if (left.is_integer() && right.is_float()) {
        return -1;
    }
    if (left.is_float() && right.is_integer()) {
        return 1;
    }
    return std::nullopt;
}

int cmp_cons(Term left, Term right) {
    while (left.is_cons() && right.is_cons()) {
        if (const auto head = Terms::cmp(left.head_unchecked(), right.head_unchecked()); head != 0) {
            return head;
        }
        left = left.tail_unchecked();
        right = right.tail_unchecked();
    }
    return Terms::cmp(left, right);
}

int cmp_tuple(const Term &left, const Term &right) {
    for (std::size_t at = 0; at < left.tuple_size_unchecked(); ++at) {
        if (const auto order = Terms::cmp(left.tuple_element_unchecked(at), right.tuple_element_unchecked(at));
            order != 0) {
            return order;
        }
    }
    return 0;
}
} // namespace

Term Terms::decode(std::span<const std::byte> bytes) const {
    try {
        return decode_external(context_, {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
    } catch (const BuiltinFailure &) {
        throw BadObject{};
    }
}

Bytes Terms::encode(const Term &term) const {
    const auto bytes = encode_external(context_, term);
    const auto *begin = reinterpret_cast<const std::byte *>(bytes.data());
    return {begin, begin + bytes.size()};
}

std::uint32_t Terms::hash(const Term &key, HashMethod method) const {
    if (method == HashMethod::phash2) {
        return static_cast<std::uint32_t>(term_hash(context_, key)) & PHASH2_MASK;
    }
    return make_hash(context_, key) % BIG;
}

Term Terms::element(const Term &object, std::uint32_t position) {
    if (!object.is_tuple() || object.tuple_size_unchecked() < position || position == 0) {
        throw BadObject{};
    }
    return object.tuple_element_unchecked(position - 1);
}

int Terms::cmp(const Term &left, const Term &right) {
    if (same(left, right)) {
        return 0;
    }
    if (left.is_cons() && right.is_cons()) {
        return cmp_cons(left, right);
    }
    if (left.is_tuple() && right.is_tuple() && left.tuple_size_unchecked() == right.tuple_size_unchecked()) {
        return cmp_tuple(left, right);
    }
    if (const auto numbers = cmp_numbers(left, right)) {
        return *numbers;
    }
    return ordered(left, right);
}

bool Terms::same(const Term &left, const Term &right) { return collections::same(left, right); }
} // namespace clause::runtime::builtins::dets
