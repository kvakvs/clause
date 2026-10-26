#include "../artifacts/artifacts.hpp"
#include "../project/command.hpp"
#include "../project/paths.hpp"
#include "asn1_sources.hpp"
#include "environment.hpp"
#include "frontend.hpp"
#include "options.hpp"
#include "publication.hpp"
#include "stage_times.hpp"
#include <iostream>
#include <optional>
#include <set>
#include <utility>

namespace clause::cli {
namespace {
// Resolve artifact roots independently of the target's executable output.
BackendOptions target_options(const Options &options, const project::Invocation &invocation,
                              const project::PlannedTarget &target) {
    auto backend = options.backend;
    const auto root = backend.artifact_directory
                          ? project::absolute_path(std::filesystem::current_path(), *backend.artifact_directory)
                          : invocation.file.parent_path() / "build" / "aot";
    backend.artifact_directory = root / artifacts::encoded_name(target.name);
    backend.keep_exports = backend.keep_exports || target.keep_exports_;
    backend.shell = backend.shell || target.shell_;
    return backend;
}

// Protect every selected input and the manifest, including aliases into another target's batch.
std::vector<std::filesystem::path> protected_inputs(const project::Invocation &invocation) {
    std::vector<std::filesystem::path> inputs{invocation.file};
    for (const auto &target : invocation.targets) {
        inputs.insert(inputs.end(), target.sources.begin(), target.sources.end());
    }
    return inputs;
}

// Report a publication failure with the target that produced the output.
int publication_failure(const std::string &target, const std::string_view message) {
    std::cerr << "clau: error: target " << target << ": " << message << '\n';
    return 1;
}

// Compare destinations through existing links where possible, else lexically.
std::filesystem::path normalized(const std::filesystem::path &path) {
    std::error_code error;
    auto result = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : result;
}

// Refuse two executables whose final names (after ".exe" normalization) are the same file.
const PendingExecutable *colliding(const std::vector<PendingExecutable> &pending, const PendingExecutable &current) {
    for (const auto &earlier : pending) {
        if (&earlier == &current) {
            return nullptr;
        }
        if (normalized(earlier.executable.output) == normalized(current.executable.output)) {
            return &earlier;
        }
    }
    return nullptr;
}

// Replace executable outputs only after every target linked and no two targets share an output.
int publish_executables(const std::vector<PendingExecutable> &pending) {
    for (const auto &item : pending) {
        if (const auto *earlier = colliding(pending, item)) {
            return publication_failure(item.project_target,
                                       "executable output " + project::path_text(item.executable.output) +
                                           " is also the output of target " + earlier->project_target);
        }
    }
    for (const auto &item : pending) {
        try {
            linking::publish_executable(item.executable);
        } catch (const std::exception &error) {
            return publication_failure(item.project_target, error.what());
        }
    }
    return 0;
}

// Print one target's environment with the artifact root and executable plan the build would use.
bool print_target_environment(const Options &options, const project::Invocation &invocation,
                              const project::PlannedTarget &target) {
    const auto backend = target_options(options, invocation, target);
    print_environment_target({.name = target.name,
                              .project = invocation.file,
                              .sources = target.sources,
                              .search_paths = target.search_paths_,
                              .preprocessing = target.preprocessing,
                              .backend = backend,
                              .output = target.output,
                              .entry = target.entry,
                              .ignore = &target.ignore_,
                              .build_directory = target.build_directory_,
                              .cache_dirs = &target.cache_dirs_});
    return false;
}

// Defer all project publication until every selected target has compiled successfully.
int publish_targets(const std::vector<Publication> &pending) {
    for (const auto &batch : pending) {
        try {
            publish(batch);
        } catch (const std::exception &error) {
            return publication_failure(batch.project_target, error.what());
        }
    }
    return 0;
}

// Plan targets with the invocation's overrides; frontend-only requests never plan executable outputs. Pattern walks
// feed `index` (62E).
project::PlanOptions plan_settings(const Options &options, project::DirectoryIndex *index = nullptr) {
    project::PlanOptions settings;
    settings.discovery.index = index;
    settings.working_directory = std::filesystem::current_path();
    settings.preprocessing = options.preprocessing;
    settings.frontend = options.preprocess || options.backend.emit.has_value() || options.backend.inspect_ir() ||
                        options.backend.print_types;
    if (options.output_explicit) {
        settings.output = options.output;
    }
    settings.entry = options.entry;
    settings.build_directory = options.build_directory;
    settings.cache_inference_in_dirs = options.cache_inference_in_dirs; // Added for the inference cache (58U).
    if (options.print_stage_times) {
        // Added for the directory scan measurement (62D3).
        settings.planned = [](const std::filesystem::path &manifest, const std::chrono::steady_clock::duration time) {
            print_stage_time("plan", project::path_text(manifest), time);
        };
    }
    return settings;
}

// The modules one ASN.1 group generates, its specifications' directories searched for imports; nothing after
// reporting a failure. Added for ASN.1 sources.
std::optional<std::vector<std::filesystem::path>> group_modules(const project::PlannedTarget &target,
                                                                const project::PlannedAsn1 &group,
                                                                const project::MessageSink &sink) {
    if (const auto error = check_asn1_options(group.options)) {
        sink("error: target " + target.name + ": " + *error);
        return std::nullopt;
    }
    Asn1Settings settings{group.options, group.outdir, {}};
    settings.include_dirs_.reserve(group.sources.size());
    for (const auto &source : group.sources) {
        settings.include_dirs_.push_back(source.parent_path());
    }
    return generate_asn1(group.sources, settings, sink);
}

// The target's Erlang sources and the modules its ASN.1 groups generate (a generated module already among the
// sources is not added twice); nothing after reporting a failure. Added for ASN.1 sources.
std::optional<std::vector<std::filesystem::path>> target_inputs(const project::PlannedTarget &target,
                                                                const project::MessageSink &sink) {
    auto sources = target.sources;
    std::set<std::filesystem::path> known;
    for (const auto &source : sources) {
        known.insert(source.lexically_normal());
    }
    for (const auto &group : target.asn1_) {
        const auto generated = group_modules(target, group, sink);
        if (!generated) {
            return std::nullopt;
        }
        std::ranges::copy_if(*generated, std::back_inserter(sources),
                             [&](const auto &module) { return known.insert(module.lexically_normal()).second; });
    }
    return sources;
}

// Compile the target's sources; the modules its ASN.1 groups generate join the batch, their headers the include path.
// True on failure. Added for ASN.1 sources.
bool process_target(const project::PlannedTarget &target, FrontendRequest &request, const bool generate_only,
                    const project::MessageSink &sink) {
    const auto sources = target_inputs(target, sink);
    if (!sources || generate_only) {
        return !sources;
    }
    for (const auto &group : target.asn1_) {
        request.preprocessing.include_paths.push_back(group.outdir);
        if (request.index_ != nullptr) {
            request.index_->forget(group.outdir);
        }
    }
    return process_files(*sources, request, sink);
}

// Report planned targets as TOML without processing their sources (--print-env).
int report_project(const Options &options, const std::span<char *const> arguments) {
    bool header = false;
    const project::TargetExecutor execute = [&](const project::Invocation &invocation,
                                                const project::PlannedTarget &target, const project::MessageSink &) {
        if (!std::exchange(header, true)) {
            print_environment_header(arguments, options);
        }
        return print_target_environment(options, invocation, target);
    };
    return project::run(options.project, plan_settings(options), execute, std::cout, std::cerr);
}

// A source listed literally in `sources` that the ignore rules match is compiled anyway: warn once for each.
// Added for plan step 41K2 (user direction 2026-10-12).
void warn_kept(const project::PlannedTarget &target, const project::MessageSink &sink) {
    for (const auto &source : target.ignore_.kept()) {
        sink("warning: " + project::path_text(source) +
             ": listed in sources but matched by ignore_modules or ignore_dirs; ignoring it has no effect");
    }
}
} // namespace

int run_project(const Options &options, const std::span<char *const> arguments) {
    if (options.print_env) {
        return report_project(options, arguments);
    }
    project::DirectoryIndex index;       // Added for the directory index (62E).
    BuildCaches caches{options.verbose}; // Added for build caches (62D2).
    std::vector<Publication> pending;
    std::vector<PendingExecutable> executables;
    const project::TargetExecutor execute = [&](const project::Invocation &invocation,
                                                const project::PlannedTarget &target,
                                                const project::MessageSink &sink) {
        FrontendRequest request{options.print_pp,
                                options.print_ast,
                                options.parse_check,
                                !options.preprocess || options.print_inputs,
                                options.verbose,
                                target.preprocessing,
                                target_options(options, invocation, target)};
        request.list_inputs = options.print_inputs;      // Added for --print-inputs.
        request.stage_times = options.print_stage_times; // Added for --print-stage-times.
        request.project_target = target.name;
        request.print_source = options.print_source;
        request.print_abstr = options.print_abstr;
        request.transforms_ = options.transforms;
        for (const auto &name : target.transforms_) {
            request.transforms_.transforms_.push_back(Source(0, "module", name).text);
        }
        std::ranges::copy(target.transform_paths_, std::back_inserter(request.transforms_.code_paths_));
        request.executable_output = target.output;
        request.create_output_directory = !options.output_explicit;
        request.entry = target.entry;
        request.multiple_targets = invocation.targets.size() > 1;
        request.protected_inputs = protected_inputs(invocation);
        request.pending_publications = &pending;
        request.pending_executables = &executables;
        request.module_search_paths_ = target.search_paths_;
        request.ignore_ = target.ignore_;
        warn_kept(target, sink);
        request.index_ = &index;
        request.build_directory_ = target.build_directory_;
        request.caches_ = options.no_cache ? nullptr : &caches;
        request.cache_dirs_ = target.cache_dirs_; // Added for the inference cache (58W).
        return process_target(target, request, options.generate_asn1, sink);
    };
    const int status = project::run(options.project, plan_settings(options, &index), execute, std::cout, std::cerr);
    caches.prune();
    if (options.verbose) {
        trace_index(index);
    }
    if (status != 0) {
        return status;
    }
    const int published = publish_targets(pending);
    return published == 0 ? publish_executables(executables) : published;
}
} // namespace clause::cli
