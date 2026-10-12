#pragma once
// Added for build caches (plan step 62D2): one project-internal cache interface. Callers address entries by input
// directory, file name, result kind and key and never see paths; plan step 62D5 replaced the first file backend
// with SQLite without changing that (docs/projects.md#build-caches).
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clause::cache {
// A cache key: the 128-bit content hash of everything a result depends on, as 32 lowercase hex digits.
using Key = std::string;

struct Address {
    // The input's directory (entries are grouped by it), its file name there, and the kind of result.
    std::filesystem::path directory_;
    std::string file_;
    std::string kind_;
};

// One named part of a stored value, so callers need no codec of their own. Added for the SQLite backend (62D5).
struct Part {
    // What the part holds (for example "diagnostic"), and its bytes.
    std::string name_;
    std::string bytes_;
};

// A stored value: the caller's format version of its parts, and the parts in order. Added for the SQLite
// backend (62D5).
struct Value {
    std::uint32_t format_ = 0;
    std::vector<Part> parts_;
};

// Why a lookup found nothing: no entry, an entry for another key or format, or one that failed its integrity checks.
enum class Miss : std::uint8_t { none, absent, stale, corrupt };

struct Lookup {
    // The stored value on a hit; otherwise why there is none.
    std::optional<Value> value_;
    Miss miss_ = Miss::absent;
};

// A cache store. Every operation is safe to call from several threads at once (the SQLite backend serializes
// them behind one connection and a mutex), and several processes may share one store's files.
class Store {
  public:
    Store() = default;
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    virtual ~Store() = default;
    // The value stored for `address` under `key` in `format`; a missing, stale or damaged entry is a miss, never an
    // error.
    virtual Lookup get(const Address &address, const Key &key, std::uint32_t format) = 0;
    // Store a value for `address` under `key`, replacing an older entry; a failed write is ignored (rebuilt later).
    virtual void put(const Address &address, const Key &key, const Value &value) = 0;
    // Forget the entry of `address`.
    virtual void invalidate(const Address &address) = 0;
    // Drop entries unused for too long, and files no entry names any more.
    virtual void prune() = 0;
};

// How a store keeps and ages its entries. Added for the SQLite backend (62D5).
struct StoreOptions {
    // Parts up to this size live in the database; larger ones in content-addressed blob files.
    std::size_t inline_limit_ = std::size_t{256} << 10U;
    // prune drops entries not used for this long, and blob files no entry names once they are this old.
    std::chrono::seconds max_age_ = std::chrono::hours(24 * 30);
    std::chrono::seconds orphan_age_ = std::chrono::minutes(10);
    // Where one-line notes go (a cache that falls back or turns itself off); none to stay silent.
    std::ostream *notes_ = nullptr;
};

// The SQLite backend below `root` (<build_dir>/cache): `v2/cache.db` and `v2/blobs/<hash>`
// (docs/projects.md#build-caches).
std::unique_ptr<Store> open_store(const std::filesystem::path &root, const StoreOptions &options = {});
// A miss reason as --verbose prints it.
std::string_view miss_text(Miss miss);
// The 128-bit content hash of `bytes`, as 32 lowercase hex digits.
std::string hash_text(std::string_view bytes);
// This executable's path, size and modification time: a rebuilt clau never reuses an older one's results.
std::string program_identity();
// The first part named `name`, if any. Added for the SQLite backend (62D5).
const Part *find_part(const Value &value, std::string_view name);

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
