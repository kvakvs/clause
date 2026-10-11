#pragma once
#include "glob.hpp"
#include <unordered_map>
#include <unordered_set>

namespace clause::project {
// The sources and modules a target leaves out (ignore_dirs, ignore_modules). Added for ignored modules.
class Ignore {
  public:
    Ignore() = default;
    // Resolve ignore_dirs against the manifest directory; patterns are matched lexically, never expanded.
    Ignore(const std::filesystem::path &base, const TargetOptions &options);

    // Whether the source lies in an ignored directory or is named after an ignored module.
    bool skips(const std::filesystem::path &source) const;

    // Whether ignore_modules names the module.
    bool names(std::string_view module) const { return modules_.contains(module); }

    // Whether a left-out source was named after the module.
    bool skipped(std::string_view module) const { return skipped_.contains(module); }

    // Remember a left-out source's module name (its file stem).
    void note(const std::filesystem::path &source);

    // The ignore_dirs spellings, and the ignore_modules names sorted, for --print-env.
    const std::vector<std::string> &patterns() const { return patterns_; }

    std::vector<std::string> modules() const;

  private:
    struct TextHash {
        // Hash std::string and std::string_view alike so lookups need no temporary string.
        using is_transparent = void;

        std::size_t operator()(std::string_view text) const { return std::hash<std::string_view>{}(text); }
    };

    using Names = std::unordered_set<std::string, TextHash, std::equal_to<>>;

    struct Root {
        // Everything below the root is ignored, or only paths below it that one of the wildcard rests matches.
        bool whole_ = false;
        std::vector<Glob> globs_;
    };

    // Whether the path below a root (relative to it), or one of its leading directories, matches the root's rules.
    static bool covered(const Root &root, const std::filesystem::path &relative);

    // Rules by the literal leading directory of each ignore_dirs entry (UTF-8 generic spelling), so a source is
    // checked with one hash lookup per ancestor directory; and the manifest spellings in order.
    std::unordered_map<std::string, Root, TextHash, std::equal_to<>> roots_;
    std::vector<std::string> patterns_;
    // Module names from ignore_modules, and the stems of sources left out during discovery.
    Names modules_;
    Names skipped_;
};
} // namespace clause::project
