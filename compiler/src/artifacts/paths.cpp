#include "paths.hpp"
#include "../project/paths.hpp"
#include "../semantic/symbols.hpp"
#include <set>
#include <stdexcept>

namespace clause::artifacts {
std::string encoded_name(const std::string_view identity) {
    return semantic::encode_symbol({std::string(identity), "", 0});
}

namespace detail {
namespace {
// Preserve target object format independently of the compiler host's platform.
std::string extension(const codegen::OutputKind kind, const std::string_view object_extension) {
    switch (kind) {
    case codegen::OutputKind::llvm_ir:
        return ".ll";
    case codegen::OutputKind::llvm_bitcode:
        return ".bc";
    case codegen::OutputKind::object:
        if (object_extension == ".o" || object_extension == ".obj") {
            return std::string(object_extension);
        }
        break;
    case codegen::OutputKind::assembly:
        break;
    }
    throw std::invalid_argument("invalid artifact format extension");
}

// Compare physical identities when both paths exist, including hard links and aliased parents.
bool same_file(const std::filesystem::path &left, const std::filesystem::path &right) {
    std::error_code error;
    const bool same = std::filesystem::equivalent(left, right, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::filesystem::filesystem_error("cannot inspect artifact identity", left, right, error);
    }
    return same;
}
} // namespace

void validate_destination(const std::filesystem::path &path, const std::span<const std::filesystem::path> inputs) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::filesystem::filesystem_error("cannot inspect artifact destination", path, error);
    }
    if (std::filesystem::exists(status) && !std::filesystem::is_regular_file(status)) {
        throw std::runtime_error("artifact destination is not a regular file: " + project::path_text(path));
    }
    for (const auto &input : inputs) {
        if (std::filesystem::weakly_canonical(path) == std::filesystem::weakly_canonical(input) ||
            same_file(path, input)) {
            throw std::runtime_error("artifact destination aliases an input: " + project::path_text(path));
        }
    }
}

std::vector<Destination> plan(const std::span<const codegen::OutputBuffer> outputs, const std::filesystem::path &root,
                              const std::span<const std::filesystem::path> inputs,
                              const std::string_view object_extension) {
    std::vector<Destination> destinations;
    destinations.reserve(outputs.size());
    std::set<std::filesystem::path> names;
    const auto base = project::absolute_path(std::filesystem::current_path(), root);
    for (const auto &output : outputs) {
        // "clausev1_start" never decodes as a module symbol, so it cannot collide with module artifacts.
        const auto name = output.startup ? std::string("clausev1_start") : encoded_name(output.module_name);
        const auto path = base / (name + extension(output.kind, object_extension));
        if (!names.insert(path).second) {
            throw std::runtime_error("duplicate artifact destination: " + project::path_text(path));
        }
        validate_destination(path, inputs);
        for (const auto &previous : destinations) {
            if (same_file(previous.path, path)) {
                throw std::runtime_error("artifact destinations alias each other");
            }
        }
        destinations.push_back({&output, path});
    }
    return destinations;
}
} // namespace detail
} // namespace clause::artifacts
