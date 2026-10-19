#include "../../memory/heap_object.hpp"
#include "../erlang/md5.hpp"
#include "../typed.hpp"
#include <array>
#include <cstring>
#include <terms.hpp>
#include <vector>

// The halt log files of OTP's disk_log, read as disk_log reads them in read_only mode (plan 21 step LIB12, used by
// ets:file2tab/1,2 and tabfile_info/1; docs/builtins.md#ets): an 8-byte head (LOGMAGIC and OPENED or CLOSED), then
// items <<Size:32, BIGMAGIC:32, Term/binary>>, an item of 65528 bytes or more with the MD5 of its size after the
// magic, and items <<Size:32, MAGIC:32, Term/binary>> of the format before. Bytes that frame no item are skipped one
// at a time and counted, as disk_log's chunk/3 counts them.
namespace clause::runtime::builtins {
namespace {
constexpr std::array<std::uint8_t, 4> LOG_MAGIC{1, 2, 3, 4};
constexpr std::array<std::uint8_t, 4> OPENED{6, 7, 8, 9};
constexpr std::array<std::uint8_t, 4> CLOSED{99, 88, 77, 11};
constexpr std::uint32_t BIG_MAGIC = 1649888321; // <<98,87,76,65>>
constexpr std::uint32_t OLD_MAGIC = 203500599;  // <<12,33,44,55>>
constexpr std::size_t HEADER_BYTES = 8;
constexpr std::size_t MD5_BYTES = 16;
// Items this long or longer carry the MD5 of their size (disk_log's ?MIN_MD5_TERM).
constexpr std::uint32_t MIN_MD5_TERM = 65528;

std::uint32_t big_endian(std::span<const std::byte> bytes) {
    std::uint32_t value = 0;
    for (std::size_t at = 0; at < 4; ++at) {
        value = (value << 8U) | std::to_integer<std::uint32_t>(bytes[at]);
    }
    return value;
}

bool starts_with(std::span<const std::byte> bytes, const std::array<std::uint8_t, 4> &prefix) {
    return bytes.size() >= prefix.size() && std::memcmp(bytes.data(), prefix.data(), prefix.size()) == 0;
}

// The MD5 disk_log writes after the header of a large item: erlang:md5(<<Size:32>>).
std::array<std::byte, MD5_BYTES> size_digest(std::uint32_t size) {
    const std::array<std::byte, 4> bytes{static_cast<std::byte>(size >> 24U), static_cast<std::byte>(size >> 16U),
                                         static_cast<std::byte>(size >> 8U), static_cast<std::byte>(size)};
    Md5 digest;
    digest.update(bytes);
    return digest.finish();
}

// The items of a halt log's body and the bytes that framed none.
struct Scan {
    std::vector<std::span<const std::byte>> items;
    std::size_t bad = 0;
};

// One step at `at`: an item and the offset after it, or the skip of one bad byte; none at the end of the body.
class Scanner final {
  public:
    explicit Scanner(std::span<const std::byte> body) : body_(body) {}

    Scan run() {
        while (at_ < body_.size()) {
            if (body_.size() - at_ < HEADER_BYTES) {
                // Fewer bytes than a header are counted bad, as a last read shorter than a header counts them.
                scan_.bad += body_.size() - at_;
                break;
            }
            step();
        }
        return std::move(scan_);
    }

  private:
    void step() {
        const auto size = big_endian(body_.subspan(at_, 4));
        const auto magic = big_endian(body_.subspan(at_ + 4, 4));
        const auto rest = body_.size() - at_ - HEADER_BYTES;
        if (magic == BIG_MAGIC && size >= MIN_MD5_TERM) {
            large(size, rest);
        } else if (magic == BIG_MAGIC || magic == OLD_MAGIC) {
            small(size, rest);
        } else {
            ++at_;
            ++scan_.bad;
        }
    }

    // An item of the size given, or a cut item: what is left counts bad, as disk_log's last read counts it.
    void small(std::uint32_t size, std::size_t rest) {
        if (rest < size) {
            scan_.bad += rest;
            at_ = body_.size();
            return;
        }
        scan_.items.push_back(body_.subspan(at_ + HEADER_BYTES, size));
        at_ += HEADER_BYTES + size;
    }

    // A large item: its size's MD5 first; a wrong one skips the digest and one more byte, as disk_log does.
    void large(std::uint32_t size, std::size_t rest) {
        if (rest < MD5_BYTES) {
            scan_.bad += rest;
            at_ = body_.size();
            return;
        }
        const auto digest = size_digest(size);
        if (std::memcmp(digest.data(), body_.data() + at_ + HEADER_BYTES, MD5_BYTES) != 0) {
            at_ += HEADER_BYTES + MD5_BYTES + 1;
            ++scan_.bad;
            return;
        }
        if (rest < MD5_BYTES + size) {
            scan_.bad += rest - MD5_BYTES;
            at_ = body_.size();
            return;
        }
        scan_.items.push_back(body_.subspan(at_ + HEADER_BYTES + MD5_BYTES, size));
        at_ += HEADER_BYTES + MD5_BYTES + size;
    }

    std::span<const std::byte> body_;
    std::size_t at_ = 0;
    Scan scan_;
};

// clause_disk_log:items(Binary): {opened | closed, [ItemBinary], BadBytes} for a halt log's bytes, not_a_log_file
// for any other.
Term items(ProcessContext &context, const Term &file) {
    TermFactory factory(context);
    const auto bytes = need(file.binary_bytes());
    const auto head = std::span<const std::byte>(bytes);
    const auto opened = starts_with(head.subspan(std::min<std::size_t>(4, head.size())), OPENED);
    const auto closed = starts_with(head.subspan(std::min<std::size_t>(4, head.size())), CLOSED);
    if (!starts_with(head, LOG_MAGIC) || head.size() < HEADER_BYTES || (!opened && !closed)) {
        return need(factory.atom("not_a_log_file"_atom));
    }
    const auto scan = Scanner(head.subspan(HEADER_BYTES)).run();
    std::vector<Term> binaries;
    binaries.reserve(scan.items.size());
    for (const auto item : scan.items) {
        binaries.push_back(need(factory.binary(item)));
    }
    const std::array result{need(factory.atom(opened ? "opened"_atom : "closed"_atom)), need(factory.list(binaries)),
                            need(factory.integer(static_cast<std::int64_t>(scan.bad)))};
    return need(factory.tuple(result));
}

constexpr std::array DISK_LOG_BUILTINS{
    typed_entry<items>("clause_disk_log", "items"),
};
} // namespace
} // namespace clause::runtime::builtins

namespace clause::runtime {
std::span<const BuiltinEntry> disk_log_builtins() noexcept { return builtins::DISK_LOG_BUILTINS; }
} // namespace clause::runtime
