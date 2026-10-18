#include "../typed.hpp"
#include "ports.hpp"
#include <array>
#include <climits>
#include <zlib.h>

// erlang's checksums and iolist_to_iovec/1 (plan 21 step LIB9A, docs/builtins.md#erlang-module): adler32/1,2,
// adler32_combine/3, crc32/1,2 and crc32_combine/3 over the pinned zlib, and the iovec OTP's iol2v builds.
namespace clause::runtime::builtins {
namespace {
// The roles of the checksum arguments (typed.hpp's Named).
struct FirstRole;
struct SecondRole;
struct SizeRole;
using First = Named<FirstRole>;
using Second = Named<SecondRole>;
using Size = Named<SizeRole>;

// Binaries larger than this many bytes stay their own iovec elements; smaller ones and bytes are copied together
// (OTP's IOL2V_COPY_THRESHOLD).
constexpr std::size_t IOVEC_COPY_LIMIT = 512;

std::vector<std::byte> bytes_of(const Term &data) {
    auto bytes = iodata_bytes(data);
    if (!bytes) {
        bad_argument();
    }
    return std::move(*bytes);
}

// A checksum argument: an integer in 0..2^32-1, else badarg.
uLong checksum_of(const Term &value) {
    const auto number = small(value.word());
    if (!number || *number < 0 || *number > 0xFFFFFFFFLL) {
        bad_argument();
    }
    return static_cast<uLong>(*number);
}

// A length argument of the combine functions: a non-negative integer zlib's z_off_t holds, else badarg.
z_off_t length_of(const Term &value) {
    const auto number = small(value.word());
    if (!number || *number < 0 || *number > LONG_MAX) {
        bad_argument();
    }
    return static_cast<z_off_t>(*number);
}

uLong update_adler(uLong adler, std::span<const std::byte> bytes) {
    for (std::size_t at = 0; at < bytes.size();) {
        const auto chunk = static_cast<uInt>(std::min<std::size_t>(bytes.size() - at, UINT_MAX));
        adler = ::adler32(adler, reinterpret_cast<const Bytef *>(bytes.data() + at), chunk);
        at += chunk;
    }
    return adler;
}

uLong update_crc(uLong crc, std::span<const std::byte> bytes) {
    for (std::size_t at = 0; at < bytes.size();) {
        const auto chunk = static_cast<uInt>(std::min<std::size_t>(bytes.size() - at, UINT_MAX));
        crc = ::crc32(crc, reinterpret_cast<const Bytef *>(bytes.data() + at), chunk);
        at += chunk;
    }
    return crc;
}

Term number(ProcessContext &context, uLong value) {
    return need(TermFactory(context).integer(static_cast<std::int64_t>(value)));
}

Term adler32_1(ProcessContext &context, const Term &data) {
    return number(context, update_adler(::adler32(0, nullptr, 0), bytes_of(data)));
}

Term adler32_2(ProcessContext &context, const First &old, const Term &data) {
    const auto start = checksum_of(old.term);
    return number(context, update_adler(start, bytes_of(data)));
}

Term adler32_combine(ProcessContext &context, const First &first, const Second &second, const Size &size) {
    return number(context, ::adler32_combine(checksum_of(first.term), checksum_of(second.term), length_of(size.term)));
}

Term crc32_1(ProcessContext &context, const Term &data) {
    return number(context, update_crc(::crc32(0, nullptr, 0), bytes_of(data)));
}

Term crc32_2(ProcessContext &context, const First &old, const Term &data) {
    const auto start = checksum_of(old.term);
    return number(context, update_crc(start, bytes_of(data)));
}

Term crc32_combine(ProcessContext &context, const First &first, const Second &second, const Size &size) {
    return number(context, ::crc32_combine(checksum_of(first.term), checksum_of(second.term), length_of(size.term)));
}

// The elements of an iovec being built: binaries kept as they are and bytes copied together.
class Iovec final {
  public:
    explicit Iovec(ProcessContext &context) : context_(context) {}

    // One element of iodata: a byte (a list element), a binary, or a list to walk.
    void add(const Term &item, bool element) {
        const auto byte = element ? small(item.word()) : std::nullopt;
        if (byte && *byte >= 0 && *byte <= 255) {
            copied_.push_back(static_cast<std::byte>(*byte));
        } else if (item.is_binary()) {
            binary(item);
        } else if (item.is_cons()) {
            pending_.emplace_back(item.tail_unchecked(), false);
            pending_.emplace_back(item.head_unchecked(), true);
        } else if (!item.is_nil()) {
            bad_argument();
        }
    }

    // Walk all of `data` in order.
    Term run(const Term &data) {
        pending_.emplace_back(data, false);
        while (!pending_.empty()) {
            const auto [item, element] = pending_.back();
            pending_.pop_back();
            add(item, element);
        }
        flush();
        return need(TermFactory(context_).list(elements_));
    }

  private:
    void binary(const Term &item) {
        const auto size = need(item.binary_bytes()).size();
        if (size <= IOVEC_COPY_LIMIT) {
            const auto bytes = need(item.binary_bytes());
            copied_.insert(copied_.end(), bytes.begin(), bytes.end());
            return;
        }
        flush();
        elements_.push_back(item);
    }

    // The bytes copied since the last element become one binary.
    void flush() {
        if (!copied_.empty()) {
            elements_.push_back(need(TermFactory(context_).binary(copied_)));
            copied_.clear();
        }
    }

    ProcessContext &context_;
    // Terms still to read, each with whether it is a list element.
    std::vector<std::pair<Term, bool>> pending_;
    std::vector<std::byte> copied_;
    std::vector<Term> elements_;
};

// iolist_to_iovec(IoListOrBinary): a list of binaries with the same bytes; a binary or [Binary] stays as it is.
Term iolist_to_iovec(ProcessContext &context, const Term &data) {
    const auto single = data.is_cons() && data.tail_unchecked().is_nil() ? data.head_unchecked() : data;
    if (single.is_binary()) {
        const bool empty = need(single.binary_bytes()).empty();
        if (empty) {
            return need(TermFactory(context).nil());
        }
        return data.is_cons() ? data : need(TermFactory(context).list(std::array{data}));
    }
    if (!data.is_list()) {
        bad_argument();
    }
    return Iovec(context).run(data);
}

constexpr std::array CHECKSUM_BUILTINS{
    typed_entry<adler32_1>("erlang", "adler32"),
    typed_entry<adler32_2>("erlang", "adler32"),
    typed_entry<adler32_combine>("erlang", "adler32_combine"),
    typed_entry<crc32_1>("erlang", "crc32"),
    typed_entry<crc32_2>("erlang", "crc32"),
    typed_entry<crc32_combine>("erlang", "crc32_combine"),
    typed_entry<iolist_to_iovec>("erlang", "iolist_to_iovec"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> checksum_builtins() noexcept { return builtins::CHECKSUM_BUILTINS; }
} // namespace clause::runtime
