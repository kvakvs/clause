#pragma once
// Added for parse transforms: applying a module's parse transforms between parsing and analysis.
#include "loader.hpp"
#include <clause/compiler/parser.hpp>
#include <functional>

namespace clause::transforms {
// Whether the host OTP provides a module it never replaces (a sticky one). Added for transform source caching.
using HostModule = std::function<bool(const std::u32string &)>;

struct TransformSettings {
    // --erl; transforms named by --parse-transform or the project, run before the module's own; directories of
    // precompiled transform modules.
    std::optional<std::filesystem::path> erl_;
    std::vector<std::u32string> transforms_;
    std::vector<std::filesystem::path> code_paths_;
    // The sources of a project transform module and of the project modules it calls, by module name, leaving out
    // host modules; the loader compiles them when no .beam on the code paths precedes them.
    std::function<std::vector<std::filesystem::path>(const std::u32string &, const HostModule &)> find_sources_;
    // The host's sticky modules, shared by every copy of the settings so erl is asked once per invocation.
    // Added for transform source caching.
    std::shared_ptr<StickyModules> sticky_ = std::make_shared<StickyModules>();
    // Where loader runs make their workspaces (<build_dir>/tmp); absent: the system temporary directory.
    // Added for the build directory (62D1).
    std::optional<std::filesystem::path> temporary_directory_;
    // Loader replies and sticky module lists kept between invocations; absent: none are kept. Added for the
    // transform cache (62D2).
    std::shared_ptr<HostCache> cache_;
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
};

// Run the module's transforms on the host OTP and parse the forms they return into `parser`, as erlc does
// between epp and erl_lint; `main` is the module's main source.
TransformOutcome transform_module(const ast::Module &syntax, const Source &main, const TransformSettings &settings,
                                  const CompileOptions &options, const FeatureSnapshot &features,
                                  const ParserSession &parser);
} // namespace clause::transforms
