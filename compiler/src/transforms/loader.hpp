#pragma once
// Added for parse transforms: running parse transforms through the loader on the host Erlang/OTP.
#include "export.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace clause::transforms {
// The loader could not run or answer: missing erl, start failure, time limit, no reply.
class LoaderError : public std::runtime_error {
  public:
    explicit LoaderError(const std::string &message) : std::runtime_error(message) {}
};

// Find the host erl: an explicit path (must be executable; `none` means no host OTP), else erl on PATH, else the
// Windows default install.
std::optional<std::filesystem::path> find_erl(const std::optional<std::filesystem::path> &explicit_path);

// The compile options a transform receives, as erlc would pass them.
struct CompileOptions {
    // Enabled feature names, include directories, -D definitions (NAME or NAME=TERM), command-line transforms.
    std::vector<std::string> features_;
    std::vector<std::filesystem::path> includes_;
    std::vector<std::string> defines_;
    std::vector<std::u32string> transforms_;
};

// A project transform module source the loader compiles in memory before running transforms.
struct TransformSource {
    std::filesystem::path path_;
    CompileOptions options_;
};

struct TransformRequest {
    // The host erl, the transforms in order, extra code paths, sources to build, the module's file and options.
    std::filesystem::path erl_;
    std::vector<std::u32string> transforms_;
    std::vector<std::filesystem::path> code_paths_;
    std::vector<TransformSource> sources_;
    std::string file_;
    CompileOptions options_;
    // Where the run's workspace is made; absent: the system temporary directory. Added for the build directory.
    std::optional<std::filesystem::path> temporary_directory_ = {};
};

struct TransformMessage {
    // The file and location a transform reported (no location: none), and the formatted text.
    std::string file_;
    std::optional<std::pair<std::size_t, std::size_t>> location_;
    std::string text_;
};

struct TransformReply {
    // Whether the chain finished; the resulting forms (in `terms_`), the messages, and the loader's own output.
    bool ok_ = false;
    Terms terms_;
    std::vector<TermId> forms_;
    std::vector<TransformMessage> errors_;
    std::vector<TransformMessage> warnings_;
    std::string output_;
};

// Run the request's transforms over the module's forms on the host OTP. A loader that cannot run or answer throws
// LoaderError; transform failures are errors of the reply.
TransformReply run_transforms(const AbstractModule &module, const TransformRequest &request);

// The modules a host erl loads from its sticky directories (kernel, stdlib and compiler ebin), which cannot be
// replaced: their project sources are never compiled. Asked once per erl, then kept in memory.
// Added for transform source caching.
class StickyModules {
  public:
    // Whether the host loads `module` from a sticky directory; when erl cannot answer, no module is. The query's
    // workspace goes below `temporary` when given.
    bool contains(const std::filesystem::path &erl, const std::u32string &module,
                  const std::optional<std::filesystem::path> &temporary = {});

  private:
    // The sticky module names of each erl asked.
    std::map<std::filesystem::path, std::set<std::u32string>> modules_;
};
} // namespace clause::transforms
