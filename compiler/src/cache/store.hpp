#pragma once
// Added for build caches (plan step 62D2): one project-internal cache interface. Callers address entries by input
// directory, file name, result kind and key and never see paths, so a later backend (SQLite or similar) can replace
// the file backend without changing them (docs/projects.md#build-caches).
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace clause::cache {
// A cache key: the 128-bit content hash of everything a result depends on, as 32 lowercase hex digits.
using Key = std::string;

struct Address {
    // The input's directory (entries are grouped by it), its file name there, and the kind of result.
    std::filesystem::path directory_;
    std::string file_;
    std::string kind_;
};

// Why a lookup found nothing: no entry, an entry for another key, or one that failed its integrity checks.
enum class Miss : std::uint8_t { none, absent, stale, corrupt };

struct Lookup {
    // The stored bytes on a hit; otherwise why there are none.
    std::optional<std::string> bytes_;
    Miss miss_ = Miss::absent;
};

class Store {
  public:
    Store() = default;
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    virtual ~Store() = default;
    // The bytes stored for `address` under `key`; a missing, stale or damaged entry is a miss, never an error.
    virtual Lookup get(const Address &address, const Key &key) = 0;
    // Store bytes for `address` under `key`, replacing an older entry; a failed write is ignored (rebuilt later).
    virtual void put(const Address &address, const Key &key, std::string_view bytes) = 0;
    // Forget the entry of `address`.
    virtual void invalidate(const Address &address) = 0;
    // Drop superseded results of the directories this store wrote to.
    virtual void prune() = 0;
};

// The file backend below `root` (<build_dir>/cache): `v<format>/<directory tail>-<path hash>/` holds one index and
// append-only pack files per result kind.
std::unique_ptr<Store> file_store(const std::filesystem::path &root);
// A miss reason as --verbose prints it.
std::string_view miss_text(Miss miss);
// The 128-bit content hash of `bytes`, as 32 lowercase hex digits.
std::string hash_text(std::string_view bytes);
// This executable's path, size and modification time: a rebuilt clau never reuses an older one's results.
std::string program_identity();

// Little-endian fixed-width numbers and length-prefixed strings, the parts stored values are made of.
void put_number(std::string &out, std::uint64_t value);
void put_text(std::string &out, std::string_view text);

// Reads the parts put_number and put_text wrote; a short or damaged value reads as none.
class Reader {
  public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    std::optional<std::uint64_t> number();
    std::optional<std::string> text();

    [[nodiscard]] bool empty() const { return bytes_.empty(); }

  private:
    // What is left to read.
    std::string_view bytes_;
};

// Collects the parts a key depends on, each length-prefixed so different splits never collide.
class KeyBuilder {
  public:
    // Add one part.
    KeyBuilder &add(std::string_view part);
    // Add a file's path and content (or that it is missing).
    KeyBuilder &add_file(const std::filesystem::path &path);

    // The key of everything added.
    Key finish() const { return hash_text(bytes_); }

  private:
    std::string bytes_;
};
} // namespace clause::cache
