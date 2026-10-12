#include "engine.hpp"
#include <algorithm>

namespace clause {
std::string PreprocessorSession::State::expand_environment(std::string name) const {
    if (name.empty() || name.front() != '$') {
        return name;
    }
    const auto slash = name.find_first_of("/\\");
    const auto key = std::string_view(name).substr(1, slash == std::string::npos ? slash : slash - 1);
    const auto value = options.environment(key);
    if (value) {
        name.replace(0, slash == std::string::npos ? name.size() : slash, *value);
    }
    return name;
}

namespace {
// Application lookup is a deterministic fallback after ordinary include paths.
void append_application(std::vector<std::filesystem::path> &result, const std::filesystem::path &path,
                        const PreprocessorOptions &options) {
    if (path.begin() != path.end()) {
        auto component = path.begin();
        const auto app = std::find_if(options.applications.rbegin(), options.applications.rend(),
                                      [&](const auto &entry) { return entry.first == component->string(); });
        if (app != options.applications.rend()) {
            auto candidate = app->second;
            if (candidate.is_relative()) {
                candidate = options.working_directory / candidate;
            }
            for (++component; component != path.end(); ++component) {
                candidate /= *component;
            }
            result.push_back(std::move(candidate));
        }
    }
}
} // namespace

std::vector<std::filesystem::path> PreprocessorSession::State::search_bases() const {
    auto directory = files.back().path.parent_path();
    if (directory.is_relative()) {
        directory = options.working_directory / directory;
    }
    std::vector<std::filesystem::path> result{directory, options.working_directory};
    result.reserve(2 + options.include_paths.size());
    for (auto include : options.include_paths) {
        if (include.is_relative()) {
            include = options.working_directory / include;
        }
        result.push_back(std::move(include));
    }
    return result;
}

std::vector<std::filesystem::path> PreprocessorSession::State::candidates(const std::string &name,
                                                                          const bool library) const {
    const std::filesystem::path path(name);
    if (path.is_absolute()) {
        return {path};
    }
    auto result = search_bases();
    for (auto &base : result) {
        base /= path;
    }
    if (library) {
        append_application(result, path, options);
    }
    return result;
}

bool PreprocessorSession::State::open_versioned(const std::filesystem::path &name, const Token &site) {
    if (name.is_absolute() || std::distance(name.begin(), name.end()) < 2) {
        return false;
    }
    const auto application = *name.begin(); // A copy: a path iterator may own the element it yields.
    const auto rest = name.lexically_relative(application);
    for (const auto &base : search_bases()) {
        const auto match = options.application_versions->find(base, application);
        if (!match) {
            continue;
        }
        if (!match->duplicates_.empty() && options.application_versions->first_report(base, application)) {
            report_duplicates(*match, site);
        }
        if (open_include(match->directory_ / rest, site)) {
            return true;
        }
    }
    return false;
}

void PreprocessorSession::State::report_duplicates(const ApplicationVersions::Match &match, const Token &site) {
    std::string names;
    for (const auto &name : match.duplicates_) {
        names += (names.empty() ? "" : ", ") + name;
    }
    const auto base = match.directory_.parent_path().generic_u8string();
    const auto chosen = match.directory_.filename().u8string();
    diagnostic({DiagnosticCode::duplicate_application,
                "duplicate versioned application directories in " + std::string(base.begin(), base.end()) + ": " +
                    names + "; using " + std::string(chosen.begin(), chosen.end()),
                site.spelling, site.origins, Severity::warning, site.location});
}

void PreprocessorSession::State::push_file(const SourcePtr &source, std::filesystem::path path, const Token &site) {
    files.push_back({source, Lexer(source), std::move(path), source->name, {}, site, 1});
    if (options.include_loaded) {
        options.include_loaded(files.back().path);
    }
    emit_file(site, source->name, 1);
}

void PreprocessorSession::State::include(const Directive &directive, const Token &site) {
    if (files.size() >= options.limits.include_depth) {
        pp_fail(DiagnosticCode::resource_limit, "include depth exhausted", site);
    }
    const auto &tokens = std::get<TokenOperand>(directive.operand).tokens;
    std::u32string name;
    for (const auto &token : tokens) {
        if (token.kind != TokenKind::string) {
            pp_fail(DiagnosticCode::malformed_directive, "include expects literal strings", token);
        }
        name += token.text();
    }
    const auto filename = expand_environment(utf8(name));
    const bool library = directive.kind == DirectiveKind::include_lib;
    for (const auto &path : candidates(filename, library)) {
        if (open_include(path, site)) {
            return;
        }
    }
    if (library && open_versioned(filename, site)) {
        return;
    }
    pp_fail(DiagnosticCode::include_not_found, "cannot find include " + filename, site);
}

bool PreprocessorSession::State::open_include(const std::filesystem::path &path, const Token &site) {
    if (options.may_exist && !options.may_exist(path)) {
        return false;
    }
    const auto bytes = options.read_file(path);
    if (!bytes) {
        return false;
    }
    if (options.include_read) {
        options.include_read(path, *bytes);
    }
    try {
        push_file(sources.add(path.string(), *bytes), path, site);
    } catch (const EncodingError &error) {
        pp_fail(DiagnosticCode::invalid_encoding, path.string() + ": " + error.what(), site);
    }
    return true;
}
} // namespace clause
