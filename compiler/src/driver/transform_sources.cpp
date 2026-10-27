// Added for parse transforms: a project transform module's sources and those of the project modules it calls.
#include "transform_sources.hpp"
#include "../semantic/calls.hpp"
#include "transforms/export.hpp"
#include <algorithm>
#include <clause/compiler/parser.hpp>
#include <set>

namespace clause::cli {
namespace {
// Module.erl among the inputs, then in the directories, passing over ignored ones; empty when there is none.
std::filesystem::path locate(const std::u32string &module, const SourcePlaces &places) {
    const auto bytes = utf8(module) + ".erl";
    const std::filesystem::path file(std::u8string(bytes.begin(), bytes.end()));
    const auto input = std::ranges::find_if(places.inputs_, [&](const auto &path) { return path.filename() == file; });
    if (input != places.inputs_.end()) {
        return *input;
    }
    for (const auto &directory : places.directories_) {
        std::error_code error;
        if (places.index_ && !places.index_->may_exist(directory / file)) {
            continue;
        }
        if (std::filesystem::is_regular_file(directory / file, error) && !places.ignore_.skips(directory / file)) {
            return directory / file;
        }
    }
    return {};
}

// The modules a source calls by literal name; none when it does not parse here (the host compiler decides). The
// files it includes are added to `includes`.
std::vector<std::u32string> called_modules(const std::filesystem::path &path, PreprocessorOptions options,
                                           std::vector<std::filesystem::path> &includes) {
    options.include_loaded = [&includes](const std::filesystem::path &file) { includes.push_back(file); };
    try {
        SourceManager sources;
        PreprocessorSession session(sources.read(path), options);
        const auto parsed = parse_module(session);
        std::vector<std::u32string> result;
        for (const auto &[name, site] : semantic::referenced_modules(parsed.module)) {
            result.push_back(name);
        }
        return result;
    } catch (const std::exception &) {
        return {};
    }
}

// called_modules of a source, parsed once per batch. Added for transform source caching.
const std::vector<std::u32string> &cached_calls(const std::filesystem::path &path, const PreprocessorOptions &options,
                                                TransformSourceCache &cache) {
    auto found = cache.calls_.find(path);
    if (found == cache.calls_.end()) {
        found = cache.calls_.emplace(path, called_modules(path, options, cache.includes_[path])).first;
    }
    return found->second;
}

// Walk the calls from `module` to the project sources it needs; host modules end a branch.
std::vector<std::filesystem::path> discover(const std::u32string &module, const SourcePlaces &places,
                                            const PreprocessorOptions &options, const transforms::HostModule &host,
                                            TransformSourceCache &cache) {
    std::vector<std::filesystem::path> result;
    std::set<std::u32string> seen{module};
    std::vector<std::u32string> pending{module};
    while (!pending.empty()) {
        const auto name = pending.back();
        pending.pop_back();
        auto path = host(name) ? std::filesystem::path{} : locate(name, places);
        if (path.empty()) {
            continue;
        }
        for (const auto &called : cached_calls(path, options, cache)) {
            if (seen.insert(called).second) {
                pending.push_back(called);
            }
        }
        result.push_back(std::move(path));
    }
    return result;
}
} // namespace

std::vector<std::filesystem::path> transform_sources(const std::u32string &module, const SourcePlaces &places,
                                                     const PreprocessorOptions &options,
                                                     const transforms::HostModule &host, TransformSourceCache &cache) {
    // Added for transform source caching.
    auto found = cache.sources_.find(module);
    if (found == cache.sources_.end()) {
        found = cache.sources_.emplace(module, discover(module, places, options, host, cache)).first;
    }
    return found->second;
}

namespace {
// Keep only the errors of a source's diagnostics: its warnings belong to its own build, not to the loader's.
void add_errors(std::vector<Diagnostic> diagnostics, transforms::SourceForms &result) {
    for (auto &diagnostic : diagnostics) {
        if (diagnostic.severity == Severity::error) {
            result.errors_.push_back(std::move(diagnostic));
        }
    }
}

// The compile options a transform source's own transforms get: the batch's directories and defines.
transforms::CompileOptions source_options(const PreprocessorOptions &options, const FeatureSnapshot &features) {
    return {.features_ = features ? features->enabled : std::vector<std::string>{},
            .includes_ = options.include_paths,
            .defines_ = options.definitions,
            .transforms_ = {}};
}

// A parsed source after its own transforms, as forms; none after errors.
void transformed_forms(const ast::Module &syntax, const Source &source, const FeatureSnapshot &features,
                       const PreprocessorOptions &options, const transforms::TransformSettings &settings,
                       transforms::SourceForms &result) {
    ParserSession parser;
    auto outcome =
        transforms::transform_module(syntax, source, settings, source_options(options, features), features, parser);
    result.output_ = std::move(outcome.output_);
    add_errors(std::move(outcome.diagnostics_), result);
    if (outcome.failed_) {
        return;
    }
    auto finished = std::move(parser).finish(features);
    add_errors(std::move(finished.diagnostics), result);
    if (!finished.failed) {
        const auto &module = finished.module;
        result.module_ = std::make_shared<transforms::AbstractModule>(
            transforms::export_module(module, module.forms(), outcome.end_));
    }
}

// Preprocess, parse and transform one source.
transforms::SourceForms read_source(const std::filesystem::path &path, const PreprocessorOptions &options,
                                    const transforms::TransformSettings &settings) {
    transforms::SourceForms result;
    SourceManager sources;
    const auto source = sources.read(path);
    PreprocessorSession session(source, options);
    auto parsed = parse_module(session);
    add_errors(std::move(parsed.diagnostics), result);
    if (parsed.failed || session.failed()) {
        return result;
    }
    const auto &syntax = parsed.module;
    if (transforms::wants_transforms(syntax, settings)) {
        transformed_forms(syntax, *source, session.features(), options, settings, result);
    } else {
        result.module_ = std::make_shared<transforms::AbstractModule>(
            transforms::export_module(syntax, syntax.forms(), transforms::end_of(syntax, syntax.forms(), *source)));
    }
    return result;
}
} // namespace

transforms::SourceForms read_transform_source(const std::filesystem::path &path, const PreprocessorOptions &options,
                                              const transforms::TransformSettings &settings,
                                              TransformSourceCache &cache) {
    if (const auto found = cache.forms_.find(path); found != cache.forms_.end()) {
        return found->second;
    }
    if (!cache.reading_.insert(path).second) {
        // A source whose own transforms need it again cannot be read.
        return {};
    }
    transforms::SourceForms result;
    try {
        result = read_source(path, options, settings);
    } catch (const std::exception &) {
        result = {};
    }
    cache.reading_.erase(path);
    return cache.forms_.emplace(path, std::move(result)).first->second;
}
} // namespace clause::cli
