#include "options.hpp"
#include <map>

namespace clause::cli {
// Apply validated preprocessing operands to the per-module configuration.
static std::optional<std::string> apply_pp_option(const int kind, const std::string &value,
                                                  clause::PreprocessorOptions &settings) {
    switch (kind) {
    case 0:
        settings.include_paths.insert(settings.include_paths.begin(), value);
        break;
    case 1:
        settings.definitions.push_back(value);
        break;
    case 2: {
        const auto equals = value.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 == value.size()) {
            return "expected app=directory";
        }
        settings.applications.emplace_back(value.substr(0, equals), value.substr(equals + 1));
        break;
    }
    default:
        settings.features.emplace_back(value, kind == 3);
        break;
    }
    return std::nullopt;
}

// Consume one option operand, including joined -Ipath/-Dname spellings.
static std::optional<std::string> pp_option(std::string_view argument, std::span<char *> &remaining, Options &options) {
    std::string value;
    if (argument.size() > 2 && (argument.starts_with("-I") || argument.starts_with("-D"))) {
        value = argument.substr(2);
        argument = argument.substr(0, 2);
    }
    static const std::map<std::string_view, int> names{{"-I", 0},
                                                       {"--include", 0},
                                                       {"-D", 1},
                                                       {"--define", 1},
                                                       {"--app-dir", 2},
                                                       {"--enable-feature", 3},
                                                       {"--disable-feature", 4}};
    const auto found = names.find(argument);
    if (found == names.end()) {
        return "unknown option '" + std::string(argument) + "'";
    }
    if (value.empty()) {
        if (remaining.empty() || std::string_view(remaining.front()).empty()) {
            return "expected a value after " + std::string(argument);
        }
        value = remaining.front();
        remaining = remaining.subspan(1);
    }
    options.frontend_options_explicit = true;
    return apply_pp_option(found->second, value, options.preprocessing);
}

// Consume an output operand while retaining explicit option presence.
static std::optional<std::string> parse_output(const std::string_view option, std::span<char *> &remaining,
                                               Options &options) {
    if (options.output_explicit) {
        return "output path specified more than once";
    }
    if (remaining.empty() || std::string_view(remaining.front()).empty()) {
        return "expected a path after " + std::string(option);
    }
    options.output = remaining.front();
    remaining = remaining.subspan(1);
    options.output_explicit = true;
    return std::nullopt;
}

// Consume an entry operand, rejecting repetition and spellings that cannot name a function.
static std::optional<std::string> parse_entry_option(std::span<char *> &remaining, Options &options) {
    if (options.entry) {
        return "entry specified more than once";
    }
    if (remaining.empty() || std::string_view(remaining.front()).empty()) {
        return "expected MODULE or MODULE:FUNCTION after --entry";
    }
    options.entry = project::parse_entry(remaining.front());
    remaining = remaining.subspan(1);
    if (!options.entry) {
        return "--entry expects MODULE or MODULE:FUNCTION with 1..255-character names";
    }
    return std::nullopt;
}

// Consume --parse-transform MODULE, --transform-path DIR or --erl PATH. Added for parse transforms.
static std::optional<std::string> parse_transform_option(const std::string_view argument, std::span<char *> &remaining,
                                                         Options &options) {
    if (remaining.empty() || std::string_view(remaining.front()).empty()) {
        return "expected a value after " + std::string(argument);
    }
    const std::string value = remaining.front();
    remaining = remaining.subspan(1);
    auto &settings = options.transforms;
    if (argument == "--parse-transform") {
        settings.transforms_.push_back(Source(0, "module", value).text);
    } else if (argument == "--transform-path") {
        settings.code_paths_.emplace_back(value);
    } else if (settings.erl_) {
        return "--erl specified more than once";
    } else {
        settings.erl_ = value;
    }
    return std::nullopt;
}

namespace {
struct Flag {
    // Map a flag to its destination and whether it requests frontend processing.
    bool Options::*field;
    bool frontend;
};

// Keep generic mode/informational flags separate from operand-consuming options.
bool parse_flag(const std::string_view argument, Options &options) {
    static const std::map<std::string_view, Flag> flags{{"-h", {&Options::show_help, false}},
                                                        {"--help", {&Options::show_help, false}},
                                                        {"--version", {&Options::show_version, false}},
                                                        {"--preprocess-check", {&Options::preprocess, true}},
                                                        {"--print-pp", {&Options::print_pp, true}},
                                                        {"--parse-check", {&Options::parse_check, true}},
                                                        {"--print-ast", {&Options::print_ast, true}},
                                                        {"--print-source", {&Options::print_source, true}},
                                                        {"--print-abstr", {&Options::print_abstr, true}},
                                                        {"--print-inputs", {&Options::print_inputs, true}},
                                                        {"--print-env", {&Options::print_env, false}},
                                                        {"--verbose", {&Options::verbose, false}},
                                                        {"--print-stage-times", {&Options::print_stage_times, false}}};
    const auto found = flags.find(argument);
    if (found == flags.end()) {
        return false;
    }
    options.*(found->second.field) = true;
    options.preprocess = options.preprocess || found->second.frontend;
    return true;
}
} // namespace

// Delegate project operands while retaining the original generic CLI spelling rules.
static std::optional<std::string> parse_option(const std::string_view argument, std::span<char *> &remaining,
                                               Options &options) {
    if (parse_flag(argument, options)) {
        return std::nullopt;
    }
    if (argument == "-o" || argument == "--output") {
        return parse_output(argument, remaining, options);
    }
    if (argument == "--entry") {
        return parse_entry_option(remaining, options);
    }
    if (argument == "--parse-transform" || argument == "--transform-path" || argument == "--erl") {
        return parse_transform_option(argument, remaining, options);
    }
    if (is_backend_option(argument)) {
        return parse_backend_option(argument, remaining, options.backend);
    }
    if (project::is_option(argument)) {
        return project::parse_option(argument, remaining, options.project);
    }
    return pp_option(argument, remaining, options);
}

// Reject explicit outputs for check/print actions and environment reports for project creation.
static std::optional<std::string> action_conflict(const Options &options) {
    if (options.preprocess && (options.output_explicit || options.entry)) {
        return "--output and --entry cannot be used with --preprocess-check, --parse-check, --print-pp, --print-ast, "
               "--print-source, --print-abstr, or --print-inputs";
    }
    if (options.print_env && options.project.create) {
        return "--print-env cannot be combined with --new-project";
    }
    return std::nullopt;
}

// Check command combinations before honoring informational requests or reading inputs.
static std::optional<std::string> validate_options(const Options &options) {
    if (const auto error = validate_backend_options(options)) {
        return error;
    }
    if (const auto error =
            project::validate(options.project, {!options.inputs.empty(), options.output_explicit, options.preprocess,
                                                options.frontend_options_explicit, options.entry.has_value()})) {
        return error;
    }
    if (!options.show_help && !options.show_version && options.inputs.empty() && !project::active(options.project)) {
        return "no input files";
    }
    return action_conflict(options);
}

// Walk arguments in order and retain positional semantics after the end-of-options marker.
std::optional<std::string> parse_options(std::span<char *> remaining, Options &options) {
    bool positional_only = false;
    while (!remaining.empty()) {
        const std::string_view argument{remaining.front()};
        remaining = remaining.subspan(1);
        if (argument.empty()) {
            return "input path must not be empty";
        }
        if (positional_only || !argument.starts_with('-')) {
            options.inputs.emplace_back(argument);
        } else if (argument == "--") {
            positional_only = true;
        } else if (const auto error = parse_option(argument, remaining, options)) {
            return error;
        }
    }
    if (options.print_inputs) {
        // Listing replaces the other check/print actions; the frontend parses without printing.
        // Added for --print-inputs.
        options.print_pp = options.print_ast = options.print_source = options.parse_check = false;
        options.print_abstr = false;
    }
    return validate_options(options);
}
} // namespace clause::cli
