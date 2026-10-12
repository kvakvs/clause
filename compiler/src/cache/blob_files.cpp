// Added for the SQLite cache backend (plan step 62D5).
#include "blob_files.hpp"
#include "store.hpp"
#include <fstream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/Process.h>
#include <random>

namespace clause::cache {
namespace {
namespace fs = std::filesystem;

// A random name part, so concurrent clau processes never write the same temporary file.
std::string unique_suffix() {
    static std::mt19937_64 generator{
        std::random_device{}() ^
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    static constexpr std::string_view DIGITS = "0123456789abcdef";
    auto value = generator();
    std::string result(16, '0');
    for (auto &digit : result) {
        digit = DIGITS[value & 0xFU];
        value >>= 4U;
    }
    return result;
}

// A path as LLVM's file functions take it.
std::string text(const fs::path &path) {
    const auto name = path.u8string();
    return {name.begin(), name.end()};
}

// A file's status; none when it cannot be read. LLVM's calls, unlike MSVC's <filesystem>, pass the static analyzer.
std::optional<llvm::sys::fs::file_status> status_of(const fs::path &path) {
    llvm::sys::fs::file_status status;
    if (llvm::sys::fs::status(text(path), status)) {
        return std::nullopt;
    }
    return status;
}

bool has_size(const fs::path &path, const std::uint64_t size) {
    const auto status = status_of(path);
    return status && status->getSize() == size;
}

// Whether a file was last written longer than `age` ago.
bool older_than(const fs::path &path, const std::chrono::seconds age) {
    const auto status = status_of(path);
    return status && std::chrono::system_clock::now() - status->getLastModificationTime() > age;
}

// Set a file's modification time to now; false when the file cannot be opened or changed.
bool touch(const fs::path &path) {
    int descriptor = -1;
    if (llvm::sys::fs::openFileForWrite(text(path), descriptor, llvm::sys::fs::CD_OpenExisting,
                                        llvm::sys::fs::OF_Append)) {
        return false;
    }
    const auto now = std::chrono::time_point_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now());
    const auto changed = llvm::sys::fs::setLastAccessAndModificationTime(descriptor, now);
    const auto closed = llvm::sys::Process::SafelyCloseFileDescriptor(descriptor);
    return !changed && !closed;
}
} // namespace

bool BlobFiles::write(const std::string &hash, const std::string_view bytes) const {
    const auto path = directory_ / hash;
    // Already kept: refresh its time, so a concurrent prune does not take it for an orphan.
    if (has_size(path, bytes.size()) && touch(path)) {
        return true;
    }
    std::error_code error;
    fs::create_directories(directory_, error);
    const auto temporary = directory_ / (hash + "-" + unique_suffix() + ".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !file.flush()) {
            file.close();
            fs::remove(temporary, error);
            return false;
        }
    }
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        // Another process may have renamed the same content into place first.
        return has_size(path, bytes.size());
    }
    return true;
}

std::optional<std::string> BlobFiles::read(const std::string &hash, const std::uint64_t size) const {
    const auto path = directory_ / hash;
    std::ifstream file(path, std::ios::binary);
    std::string bytes(size, '\0');
    const bool whole = file && file.read(bytes.data(), static_cast<std::streamsize>(size)) && file.peek() == EOF;
    if (whole && hash_text(bytes) == hash) {
        return bytes;
    }
    file.close();
    std::error_code error;
    fs::remove(path, error);
    return std::nullopt;
}

void BlobFiles::remove_unnamed(const std::set<std::string> &named, const std::chrono::seconds age) const {
    std::error_code error;
    for (const auto &item : fs::directory_iterator(directory_, error)) {
        const auto name = item.path().filename().string();
        if (!named.contains(name) && older_than(item.path(), age)) {
            std::error_code ignored;
            fs::remove(item.path(), ignored);
        }
    }
}
} // namespace clause::cache
