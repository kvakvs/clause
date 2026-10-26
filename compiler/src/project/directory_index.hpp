#pragma once
// Added for the directory index (plan step 62E): one listing per searched directory, shared by every lookup of an
// invocation, so a lookup that misses costs no filesystem probe.
#include <cstddef>
#include <filesystem>
#include <optional>
#include <unordered_map>
#include <vector>

namespace clause::project {
class DirectoryIndex {
  public:
    struct Statistics {
        // Directories listed on first use and listings recorded by recursive walks; lookups the index answered, the
        // misses among them, and lookups it left to a filesystem probe.
        std::size_t listed_ = 0;
        std::size_t walked_ = 0;
        std::size_t lookups_ = 0;
        std::size_t misses_ = 0;
        std::size_t probes_ = 0;
    };

    // Whether `file` may exist: false only when the listing of its directory has no entry of that name. A path the
    // index cannot answer exactly (relative, with `..`, a name case folding cannot match) is left to a probe: true.
    bool may_exist(const std::filesystem::path &file);
    // Record the complete listing (entry names) of a directory a recursive walk enumerated, so it is never listed
    // again; a directory already known keeps its first listing.
    void add_listing(const std::filesystem::path &directory, const std::vector<std::filesystem::path> &names);
    // Drop a directory's listing so the next lookup lists it again: files were written there since. Added for ASN.1
    // sources.
    void forget(const std::filesystem::path &directory);

    // What the index did so far, for --verbose.
    const Statistics &statistics() const { return statistics_; }

  private:
    using Key = std::filesystem::path::string_type;

    // The id of a directory's listing, listing it on first use; none when the index cannot answer for it.
    std::optional<std::size_t> listing(const std::filesystem::path &directory);
    // Add a listing under a new id and return the id.
    std::size_t record(const Key &directory, const std::vector<std::filesystem::path> &names);

    // The listed directories by id, and every directory looked at by key: its id, or none when it could not be
    // listed.
    std::vector<Key> scanned_;
    std::unordered_map<Key, std::optional<std::size_t>> directories_;
    // The ids (ascending) of the directories holding each entry name, by name key.
    std::unordered_map<Key, std::vector<std::size_t>> names_;
    Statistics statistics_;
};
} // namespace clause::project
