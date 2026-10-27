#pragma once
// Added for parse transforms: applying a module's parse transforms between parsing and analysis.
#include "loader.hpp"
#include <clause/compiler/parser.hpp>
#include <functional>

namespace clause::transforms {
// Whether the host OTP provides a module it never replaces (a sticky one). Added for transform source caching.
using HostModule = std::function<bool(const std::u32string &)>;

// A transform source as Clause's frontend read it, after its own parse transforms: its forms, or none after the
// errors. Added for transform sources (plan 21 LIB77C).
struct SourceForms {
    std::shared_ptr<const AbstractModule> module_;
    std::vector<Diagnostic> errors_;
    // What the loader running the source's own transforms printed.
    std::string output_;
};

// Clause's own transform host: the loader linked with Clause's whole library, which runs the transforms the library
// provides without a host OTP. Added for the transform host (plan 21 LIB77A).
class ClauseHost {
  public:
    ClauseHost() = default;
    ClauseHost(const ClauseHost &) = delete;
    ClauseHost &operator=(const ClauseHost &) = delete;
    virtual ~ClauseHost() = default;
    // Whether the host has `module`: Clause's library provides it.
    virtual bool provides(const std::u32string &module) const = 0;
    // The host executable, built on first use; throws LoaderError when it cannot be built.
    virtual std::filesystem::path executable() = 0;
};

struct TransformSettings {
    // --erl; transforms named by --parse-transform or the project, run before the module's own; directories of
    // precompiled transform modules.
    std::optional<std::filesystem::path> erl_;
    std::vector<std::u32string> transforms_;
    std::vector<std::filesystem::path> code_paths_;
    // The sources of a project transform module and of the project modules it calls, by module name, leaving out
    // host modules; the loader compiles them when no .beam on the code paths precedes them.
    std::function<std::vector<std::filesystem::path>(const std::u32string &, const HostModule &)> find_sources_;
    // Reads one of those sources as the loader gets it. Added for transform sources (plan 21 LIB77C).
    std::function<SourceForms(const std::filesystem::path &)> read_source_;
    // The host's sticky modules, shared by every copy of the settings so erl is asked once per invocation.
    // Added for transform source caching.
    std::shared_ptr<StickyModules> sticky_ = std::make_shared<StickyModules>();
    // Where loader runs make their workspaces (<build_dir>/tmp); absent: the system temporary directory.
    // Added for the build directory (62D1).
    std::optional<std::filesystem::path> temporary_directory_;
    // Loader replies and sticky module lists kept between invocations; absent: none are kept. Added for the
    // transform cache (62D2).
    std::shared_ptr<HostCache> cache_;
    // Runs requests whose transforms all come from Clause's library in place of erl, and without erl those whose
    // transforms are project sources or .beam files too (LIB77C); absent: erl runs every one. Added for the
    // transform host (plan 21 LIB77A).
    std::shared_ptr<ClauseHost> host_;
};

// Whether a parsed module needs parse transforms: the settings name some, or one of its -compile attributes does.
bool wants_transforms(const ast::Module &syntax, const TransformSettings &settings);

struct TransformOutcome {
    // Problems (including what transforms reported), the loader's own output, failure, and the result's eof.
    std::vector<Diagnostic> diagnostics_;
    std::string output_;
    bool failed_ = false;
    Position end_{};
    // The loader's reply came from the cache. Added for the transform cache (62D2).
    bool cached_ = false;
    // The loader's reply bytes, for compile cache fingerprints. Added for the compile cache (62D4).
    std::string reply_;
};

// Run the module's transforms on the host OTP and parse the forms they return into `parser`, as erlc does
// between epp and erl_lint; `main` is the module's main source.
TransformOutcome transform_module(const ast::Module &syntax, const Source &main, const TransformSettings &settings,
                                  const CompileOptions &options, const FeatureSnapshot &features,
                                  const ParserSession &parser);
} // namespace clause::transforms
