#include "decode_options.hpp"
#include <set>

namespace clause::project {
namespace {
// Retain application root spelling and reject empty application identifiers.
std::map<std::string, Text> applications(const toml::table &values, const schema::Context &context) {
    const auto *node = values.get("applications");
    if (!node) {
        return {};
    }
    const auto &table = schema::table(*node, context, "applications");
    std::map<std::string, Text> result;
    for (const auto &[key, value] : table) {
        if (key.str().empty()) {
            fail(schema::site(value, context, "applications"), "empty application name");
        }
        result.emplace(std::string(key.str()), schema::text(value, context, "applications." + std::string(key.str())));
    }
    return result;
}

// Reject contradictory feature declarations before applying ordered CLI changes.
void check_features(const TargetOptions &options) {
    std::set<std::string> enabled;
    for (const auto &value : options.enable_features) {
        enabled.insert(value.value);
    }
    for (const auto &value : options.disable_features) {
        if (enabled.contains(value.value)) {
            fail(value.site, "feature appears in both enable_features and disable_features");
        }
    }
}
} // namespace

TargetOptions decode_options(const toml::node &node, const schema::Context &context) {
    const auto &values = schema::table(node, context, "options");
    schema::keys(values,
                 {"source_search_paths", "include_dirs", "defines", "enable_features", "disable_features",
                  "applications", "parse_transforms", "transform_paths", "ignore_dirs", "ignore_modules",
                  "keep_exports", "shell", "epmd_executable", "bundled_epmd", "cache_inference_in_dirs"},
                 context);
    TargetOptions result;
    result.source_search_paths = schema::strings(values, "source_search_paths", context);
    result.include_dirs = schema::strings(values, "include_dirs", context);
    result.defines = schema::strings(values, "defines", context);
    result.enable_features = schema::strings(values, "enable_features", context);
    result.disable_features = schema::strings(values, "disable_features", context);
    result.applications = applications(values, context);
    result.parse_transforms = schema::strings(values, "parse_transforms", context);
    result.transform_paths = schema::strings(values, "transform_paths", context);
    result.ignore_dirs = schema::strings(values, "ignore_dirs", context);
    result.ignore_modules = schema::strings(values, "ignore_modules", context);
    result.keep_exports_ = schema::boolean(values, "keep_exports", context);
    result.shell_ = schema::boolean(values, "shell", context);
    result.epmd_executable_ = schema::boolean(values, "epmd_executable", context);
    result.bundled_epmd_ = !values.contains("bundled_epmd") || schema::boolean(values, "bundled_epmd", context);
    result.cache_inference_in_dirs_ = schema::strings(values, "cache_inference_in_dirs", context);
    check_features(result);
    return result;
}
} // namespace clause::project
