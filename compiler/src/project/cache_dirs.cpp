// Added for the inference cache (plan step 58U).
#include "cache_dirs.hpp"
#include "paths.hpp"
#include <algorithm>

namespace clause::project {
InferenceCacheDirs::InferenceCacheDirs() {
    for (const auto pattern : IMPLICIT_INFERENCE_CACHE_DIRS) {
        add({std::string(pattern), {}});
    }
}

void InferenceCacheDirs::add(const Text &pattern) {
    const auto native = native_path(pattern.value);
    const auto text = path_text(native);
    globs_.push_back(parse_glob({native.is_absolute() ? text + "/**" : "**/" + text + "/**", pattern.site}));
    patterns_.push_back(pattern.value);
}

bool InferenceCacheDirs::covers(const std::filesystem::path &source) const {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(source, error);
    const auto directory = path_text((error ? source : absolute).lexically_normal().parent_path());
    return std::ranges::any_of(globs_, [&](const Glob &glob) { return matches(glob, directory); });
}
} // namespace clause::project
