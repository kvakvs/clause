// Added for the transform host (plan 21 LIB77A): building and finding Clause's transform host executable.
#include "host.hpp"
#include "loader.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/LockFileManager.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Program.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <sstream>
#include <vector>

namespace clause::transforms {
namespace {
namespace fs = std::filesystem;

// How long a host build, or a wait for another clau's build, may take; how much of a failed build's output shows.
constexpr unsigned BUILD_SECONDS = 600;
constexpr std::size_t LOG_LIMIT = std::size_t{16} * 1024;
// Waits for another clau's build before giving up.
constexpr int LOCK_ATTEMPTS = 3;

std::string text(const fs::path &path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

fs::path native(const llvm::SmallVectorImpl<char> &utf8) { return {std::u8string(utf8.begin(), utf8.end())}; }

// The host executable's path for `place`.
fs::path location(const HostPlace &place) {
    llvm::SmallString<256> root;
    if (!llvm::sys::path::cache_directory(root)) {
        throw LoaderError("cannot build the parse transform host: no user cache directory");
    }
    llvm::sys::path::append(root, "clause", "transform-host", place.clau_ + "-" + place.identity_);
    const auto windows = llvm::Triple(llvm::sys::getProcessTriple()).isOSWindows();
    llvm::sys::path::append(root, windows ? "clause_transform_host.exe" : "clause_transform_host");
    return native(root);
}

// A fresh directory beside the hosts for one build, removed afterwards.
class BuildArea {
  public:
    explicit BuildArea(const fs::path &host_directory) {
        llvm::SmallString<256> created;
        if (const auto error = llvm::sys::fs::createUniqueDirectory(text(host_directory) + ".build", created)) {
            throw LoaderError("cannot build the parse transform host: " + error.message());
        }
        path_ = native(created);
    }

    BuildArea(const BuildArea &) = delete;
    BuildArea &operator=(const BuildArea &) = delete;

    ~BuildArea() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path &path() const { return path_; }

  private:
    // The directory holding the loader source, the build's output and the executable until it moves.
    fs::path path_;
};

// The start of a build log, for an error.
std::string log_text(const fs::path &log) {
    std::ifstream file(log, std::ios::binary);
    std::ostringstream content;
    content << file.rdbuf();
    auto result = content.str();
    result.resize(std::min(result.size(), LOG_LIMIT));
    return result;
}

// Run this clau to link the loader with the whole library and every export kept (as --shell does), its main/1 the
// entry; the exit status, with `error` set when clau could not run.
int link_host(const fs::path &source, const fs::path &output, const std::string &log, std::string &error) {
    static int anchor = 0;
    const auto clau = llvm::sys::fs::getMainExecutable(nullptr, &anchor);
    const std::vector<std::string> arguments{
        clau, "--shell",    "--erl",     "none", "--no-cache", "--entry", "clause_transform_loader:main/1",
        "-o", text(output), text(source)};
    const std::vector<llvm::StringRef> argv(arguments.begin(), arguments.end());
    const std::array<std::optional<llvm::StringRef>, 3> redirects{llvm::StringRef(), llvm::StringRef(log),
                                                                  llvm::StringRef(log)};
    return llvm::sys::ExecuteAndWait(clau, argv, std::nullopt, redirects, BUILD_SECONDS, 0, &error);
}

// Build the host in a fresh area, then move it to `program`.
void build(const fs::path &program) {
    const BuildArea area(program.parent_path());
    const auto source = area.path() / "clause_transform_loader.erl";
    const auto bytes = loader_source();
    std::ofstream(source, std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    const auto output = area.path() / program.filename();
    const auto log = text(area.path() / "build.txt");
    std::string error;
    const auto status = link_host(source, output, log, error);
    std::error_code moved;
    if (status == 0) {
        fs::create_directories(program.parent_path(), moved);
        fs::rename(output, program, moved);
    }
    if (status != 0 || moved) {
        const auto reason = moved ? moved.message() : error;
        throw LoaderError("cannot build the parse transform host (exit status " + std::to_string(status) +
                          (reason.empty() ? "" : ", " + reason) + ")\n" + log_text(log));
    }
}

// Remove what older builds of this clau path left: entries named with the same clau hash, another identity.
void prune(const fs::path &directory, const HostPlace &place) {
    const auto name = text(directory.filename());
    std::error_code error;
    for (const auto &item : fs::directory_iterator(directory.parent_path(), error)) {
        const auto other = text(item.path().filename());
        if (other.starts_with(place.clau_ + "-") && !other.starts_with(name)) {
            std::error_code ignored; // A host still running stays until a later build.
            fs::remove_all(item.path(), ignored);
        }
    }
}
} // namespace

std::string file_listing(const std::span<const fs::path> roots) {
    std::vector<std::string> files;
    for (const auto &root : roots) {
        std::error_code error;
        files.push_back(text(root));
        for (fs::recursive_directory_iterator item(root, error), end; !error && item != end; item.increment(error)) {
            files.push_back(text(item->path()));
        }
    }
    std::ranges::sort(files);
    std::string result;
    for (const auto &file : files) {
        llvm::sys::fs::file_status status;
        if (!llvm::sys::fs::status(file, status) && status.type() == llvm::sys::fs::file_type::regular_file) {
            const auto modified = status.getLastModificationTime().time_since_epoch().count();
            result += file + " " + std::to_string(status.getSize()) + " " + std::to_string(modified) + "\n";
        }
    }
    return result;
}

fs::path ensure_host(const HostPlace &place) {
    const auto program = location(place);
    const auto directory = program.parent_path();
    std::error_code error;
    fs::create_directories(directory.parent_path(), error);
    for (int attempt = 0; attempt < LOCK_ATTEMPTS && !fs::is_regular_file(program, error); ++attempt) {
        llvm::LockFileManager lock(text(directory));
        auto owned = lock.tryLock();
        if (!owned) {
            throw LoaderError("cannot build the parse transform host: " + llvm::toString(owned.takeError()));
        }
        if (!*owned) {
            static_cast<void>(lock.waitForUnlockFor(std::chrono::seconds(BUILD_SECONDS)));
        } else if (!fs::is_regular_file(program, error)) {
            build(program);
            prune(directory, place);
        }
    }
    if (!fs::is_regular_file(program, error)) {
        throw LoaderError("cannot build the parse transform host: another clau is still building it");
    }
    return program;
}
} // namespace clause::transforms
