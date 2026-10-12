#pragma once
// Added for the SQLite cache backend (plan step 62D5): content-addressed files that hold the parts too large to keep
// in the database. A file is named by the content hash of its bytes, so equal parts share one file.
#include <chrono>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace clause::cache {
class BlobFiles {
  public:
    explicit BlobFiles(std::filesystem::path directory) : directory_(std::move(directory)) {}

    // Keep `bytes` as the file `hash` (written to a temporary file, then renamed); false when that failed.
    bool write(const std::string &hash, std::string_view bytes) const;
    // The bytes of the file `hash` when its size and content still match; a damaged file is removed.
    std::optional<std::string> read(const std::string &hash, std::uint64_t size) const;
    // Remove files older than `age` that `named` does not list, and leftover temporary files.
    void remove_unnamed(const std::set<std::string> &named, std::chrono::seconds age) const;

  private:
    // The blobs directory below the store's format directory.
    std::filesystem::path directory_;
};
} // namespace clause::cache
