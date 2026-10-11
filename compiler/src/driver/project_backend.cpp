#include "../artifacts/artifacts.hpp"
#include "../project/command.hpp"
#include "../project/paths.hpp"
#include "environment.hpp"
#include "frontend.hpp"
#include "options.hpp"
#include "publication.hpp"
#include <iostream>
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
                              .ignore = &target.ignore_});
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

// Plan targets with the invocation's overrides; frontend-only requests never plan executable outputs.
project::PlanOptions plan_settings(const Options &options) {
    project::PlanOptions settings;
    settings.working_directory = std::filesystem::current_path();
    settings.preprocessing = options.preprocessing;
    settings.frontend = options.preprocess || options.backend.emit.has_value() || options.backend.inspect_ir() ||
                        options.backend.print_types;
    if (options.output_explicit) {
        settings.output = options.output;
    }
    settings.entry = options.entry;
    return settings;
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
} // namespace

int run_project(const Options &options, const std::span<char *const> arguments) {
    if (options.print_env) {
        return report_project(options, arguments);
    }
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
        request.list_inputs = options.print_inputs; // Added for --print-inputs.
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
        return process_files(target.sources, request, sink);
    };
    const int status = project::run(options.project, plan_settings(options), execute, std::cout, std::cerr);
    if (status != 0) {
        return status;
    }
    const int published = publish_targets(pending);
    return published == 0 ? publish_executables(executables) : published;
}
} // namespace clause::cli
