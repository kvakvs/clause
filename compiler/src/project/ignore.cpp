#include "ignore.hpp"
#include "discovery.hpp"
#include "paths.hpp"
#include <algorithm>

namespace clause::project {
Ignore::Ignore(const std::filesystem::path &base, const TargetOptions &options) {
    for (const auto &pattern : options.ignore_dirs) {
        const auto [prefix, suffix] = split_pattern(native_path(pattern.value));
        auto directory = absolute_path(base, prefix);
        // A trailing separator would not match the ancestor spelling of any path below the directory.
        if (!directory.has_filename() && directory.has_relative_path()) {
            directory = directory.parent_path();
        }
        auto &root = roots_[path_text(directory)];
        if (suffix.empty()) {
            root.whole_ = true;
        } else {
            root.globs_.push_back(parse_glob({path_text(suffix), pattern.site}));
        }
        patterns_.push_back(pattern.value);
    }
    for (const auto &name : options.ignore_modules) {
        modules_.insert(name.value);
    }
}

bool Ignore::covered(const Root &root, const std::filesystem::path &relative) {
    if (root.whole_) {
        return true;
    }
    std::filesystem::path prefix;
    for (const auto &part : relative) {
        prefix /= part;
        const auto text = path_text(prefix);
        if (std::ranges::any_of(root.globs_, [&](const Glob &glob) { return matches(glob, text); })) {
            return true;
        }
    }
    return false;
}

bool Ignore::skips(const std::filesystem::path &source) const {
    if (names(path_text(source.stem()))) {
        return true;
    }
    if (roots_.empty()) {
        return false;
    }
    const auto path = source.lexically_normal();
    // Probe every ancestor directory, nearest first, until the filesystem root stops changing.
    for (auto directory = path.parent_path();; directory = directory.parent_path()) {
        const auto found = roots_.find(path_text(directory));
        if (found != roots_.end() && covered(found->second, path.lexically_relative(directory))) {
            return true;
        }
        if (directory == directory.parent_path()) {
            return false;
        }
    }
}

void Ignore::note(const std::filesystem::path &source) { skipped_.insert(path_text(source.stem())); }

std::vector<std::string> Ignore::modules() const {
    std::vector<std::string> result(modules_.begin(), modules_.end());
    std::ranges::sort(result);
    return result;
}
} // namespace clause::project
