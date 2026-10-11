#include "command.hpp"
#include "create.hpp"
#include "decode.hpp"
#include "diagnostics.hpp"
#include "paths.hpp"

namespace clause::project {
namespace {
// Complete a missing manifest filename without masking errors on an existing path.
std::filesystem::path resolve_project_file(std::filesystem::path file) {
    std::error_code error;
    if (file.extension() != ".toml" && !std::filesystem::exists(file, error) && !error) {
        file += ".toml";
    }
    return file;
}
} // namespace

int run(const Request &request, const PlanOptions &options, const TargetExecutor &executor, std::ostream &output,
        std::ostream &diagnostics) {
    try {
        if (request.create) {
            const auto path = create_project(options.working_directory, {*request.create});
            output << "Created " << path_text(path) << '\n';
            return 0;
        }
        if (!request.file) {
            fail({}, "missing --project request", 2);
        }
        const auto start = std::chrono::steady_clock::now();
        const auto file = resolve_project_file(absolute_path(options.working_directory, *request.file));
        const auto manifest = decode(load(file));
        auto settings = options;
        settings.selectors = request.targets;
        const auto invocation = prepare(manifest, settings);
        if (options.planned) {
            options.planned(file, std::chrono::steady_clock::now() - start);
        }
        return execute(invocation, executor, [&](const std::string_view message) { diagnostics << message << '\n'; });
    } catch (const Failure &error) {
        diagnostics << "clau: error: " << error.what() << '\n';
        return error.detail.exit_code;
    } catch (const std::exception &error) {
        diagnostics << "clau: error: " << error.what() << '\n';
        return 1;
    }
}
} // namespace clause::project
