#pragma once
// Added for parse transforms: running parse transforms through the loader on the host Erlang/OTP.
#include "export.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>

namespace clause::transforms {
// The loader could not run or answer: missing erl, start failure, time limit, no reply.
class LoaderError : public std::runtime_error {
  public:
    explicit LoaderError(const std::string &message) : std::runtime_error(message) {}
};

// The loader module's source, which Clause's transform host is built from. Added for the transform host (LIB77A).
std::string_view loader_source();

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

// A project transform module source as Clause's frontend read it: the loader interprets (Clause's transform host)
// or compiles (erl) its forms before running transforms. Forms added for transform sources (plan 21 LIB77C).
struct TransformSource {
    std::filesystem::path path_;
    std::shared_ptr<const AbstractModule> module_;
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
    // `erl_` is Clause's transform host, which takes the workspace as its program argument. Added for the transform
    // host (plan 21 LIB77A).
    bool clause_host_ = false;
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
    // The reply as the loader wrote it, which compile cache fingerprints hash. Added for the compile cache (62D4).
    std::string bytes_;
};

// What a host erl run depends on besides the files it reads: its kind ("transform", "sticky"), the request (erl,
// transform sources, code paths, module file) and the bytes it sends to erl. Added for the transform cache (62D2).
struct HostQuery {
    std::string_view kind_;
    const TransformRequest &request_;
    std::string_view bytes_;
};

// One host erl run as a cache keeps it: the reply file as the loader wrote it (the module list for a sticky query)
// and the loader's own output. Added for the transform cache (62D2); public since the SQLite backend (62D5).
struct HostRun {
    std::string reply_;
    std::string output_;
};

// Results of host erl runs kept between invocations; the driver backs it with the build cache. Added for the
// transform cache (62D2).
class HostCache {
  public:
    HostCache() = default;
    HostCache(const HostCache &) = delete;
    HostCache &operator=(const HostCache &) = delete;
    virtual ~HostCache() = default;
    // The result stored for the same query and inputs, if any.
    virtual std::optional<HostRun> find(const HostQuery &query) = 0;
    // Keep a fresh result.
    virtual void store(const HostQuery &query, const HostRun &run) = 0;
};

// Run the request's transforms over the module's forms on the host OTP, or reuse the reply `cache` keeps for the
// same request (then `cached` is set). A loader that cannot run or answer throws LoaderError; transform failures
// are errors of the reply.
TransformReply run_transforms(const AbstractModule &module, const TransformRequest &request, HostCache *cache = nullptr,
                              bool *cached = nullptr);

// The modules a host erl loads from its sticky directories (kernel, stdlib and compiler ebin), which cannot be
// replaced: their project sources are never compiled. Asked once per erl, then kept in memory.
// Added for transform source caching.
class StickyModules {
  public:
    // Whether the host loads `module` from a sticky directory; when erl cannot answer, no module is. The query's
    // workspace goes below `temporary` when given, and `cache` may answer it without starting erl.
    bool contains(const std::filesystem::path &erl, const std::u32string &module,
                  const std::optional<std::filesystem::path> &temporary = {}, HostCache *cache = nullptr);

  private:
    // The sticky module names of each erl asked.
    std::map<std::filesystem::path, std::set<std::u32string>> modules_;
};
} // namespace clause::transforms
