// Added for parse transforms: the transform pass between parsing and analysis (compile.erl transform_module).
#include "pipeline.hpp"
#include "abstract_input.hpp"
#include "import.hpp"
#include <algorithm>

namespace clause::transforms {
namespace {
// Whether a literal -compile option is {parse_transform, Module} with an atom module.
bool transform_option(const ast::Module &syntax, const ast::TermId &id) {
    const auto *tuple = std::get_if<ast::TermTuple>(&syntax.term(id).value);
    if (!tuple || tuple->elements.size() != 2) {
        return false;
    }
    const auto *key = std::get_if<ast::Atom>(&syntax.term(tuple->elements[0]).value);
    return key && key->name == U"parse_transform" &&
           std::holds_alternative<ast::Atom>(syntax.term(tuple->elements[1]).value);
}

// Whether a form is a -compile attribute naming a transform, alone or in a list.
bool names_transform(const ast::Module &syntax, const ast::Form &form) {
    const auto *attribute = std::get_if<ast::GenericAttribute>(&form.value);
    if (!attribute || attribute->name.name != U"compile") {
        return false;
    }
    if (transform_option(syntax, attribute->value)) {
        return true;
    }
    const auto *list = std::get_if<ast::TermList>(&syntax.term(attribute->value).value);
    return list && std::ranges::any_of(list->elements, [&](const auto &id) { return transform_option(syntax, id); });
}

// The module of a {parse_transform, Module} option term.
std::optional<std::u32string> option_transform(const Terms &terms, const TermId option) {
    const auto &node = terms.node(option);
    if (node.kind_ != TermKind::tuple || node.children_.size() != 2 ||
        !terms.is_atom(node.children_[0], U"parse_transform") ||
        terms.node(node.children_[1]).kind_ != TermKind::atom) {
        return std::nullopt;
    }
    return terms.node(node.children_[1]).atom_;
}

// The value of a {attribute, A, compile, Value} form.
std::optional<TermId> compile_value(const Terms &terms, const TermId form) {
    const auto &node = terms.node(form);
    if (node.kind_ != TermKind::tuple || node.children_.size() != 4 ||
        !terms.is_atom(node.children_[0], U"attribute") || !terms.is_atom(node.children_[2], U"compile")) {
        return std::nullopt;
    }
    return node.children_[3];
}

// The options of a -compile value: a list's elements, or the single option.
std::vector<TermId> compile_options(const Terms &terms, const TermId value) {
    const auto &node = terms.node(value);
    return node.kind_ == TermKind::list ? node.children_ : std::vector<TermId>{value};
}

// The transforms to run in compile.erl's order: the settings' ones, then those of -compile attributes.
std::vector<std::u32string> transform_names(const AbstractModule &module, const TransformSettings &settings) {
    auto result = settings.transforms_;
    for (const auto form : module.forms_) {
        const auto value = compile_value(module.terms_, form);
        for (const auto option : value ? compile_options(module.terms_, *value) : std::vector<TermId>{}) {
            if (auto name = option_transform(module.terms_, option)) {
                result.push_back(std::move(*name));
            }
        }
    }
    return result;
}

// A -compile form whose option list lost its parse_transform options.
TermId without_transform_options(Terms &terms, const TermId form, const TermId list) {
    std::vector<TermId> options;
    for (const auto option : terms.node(list).children_) {
        if (!option_transform(terms, option)) {
            options.push_back(option);
        }
    }
    auto parts = terms.node(form).children_;
    parts[3] = terms.list(std::move(options));
    return terms.tuple(std::move(parts));
}

// The forms without parse_transform options, so a transform never runs twice (compile:clean_parse_transforms/1):
// an attribute holding only such an option goes, a list loses those elements.
void remove_transform_options(AbstractModule &module) {
    auto &terms = module.terms_;
    std::vector<TermId> kept;
    kept.reserve(module.forms_.size());
    for (const auto form : module.forms_) {
        const auto value = compile_value(terms, form);
        if (value && terms.node(*value).kind_ == TermKind::list) {
            kept.push_back(without_transform_options(terms, form, *value));
        } else if (!value || !option_transform(terms, *value)) {
            kept.push_back(form);
        }
    }
    module.forms_ = std::move(kept);
}

// A diagnostic at file:line:column; line 0 locates only the file.
Diagnostic located(const std::string &file, const std::optional<std::pair<std::size_t, std::size_t>> &location,
                   std::string message, const Severity severity, SourceManager &sources) {
    Diagnostic result;
    result.code = severity == Severity::error ? DiagnosticCode::user_error : DiagnosticCode::user_warning;
    result.message = std::move(message);
    result.primary = Span{sources.add(file, ""), 0, 0};
    result.severity = severity;
    const auto [line, column] = location.value_or(std::pair<std::size_t, std::size_t>{0, 0});
    result.location = LogicalLocation{.file = file, .line = line, .column = column};
    return result;
}

// What the transforms reported, as diagnostics.
void report(const TransformReply &reply, TransformOutcome &outcome, SourceManager &sources) {
    for (const auto &message : reply.warnings_) {
        outcome.diagnostics_.push_back(
            located(message.file_, message.location_, message.text_, Severity::warning, sources));
    }
    for (const auto &message : reply.errors_) {
        outcome.diagnostics_.push_back(
            located(message.file_, message.location_, message.text_, Severity::error, sources));
    }
}

// Whether a code path directory holds Module.beam, which wins over the project's source of the module.
bool precompiled(const std::u32string &name, const std::vector<std::filesystem::path> &directories) {
    const auto bytes = utf8(name) + ".beam";
    const std::filesystem::path file(std::u8string(bytes.begin(), bytes.end()));
    return std::ranges::any_of(directories, [&](const auto &directory) {
        std::error_code error;
        return std::filesystem::is_regular_file(directory / file, error);
    });
}

// Code path directories that do not exist, reported before any host code runs.
bool missing_paths(const TransformSettings &settings, const std::string &file, TransformOutcome &outcome,
                   SourceManager &sources) {
    for (const auto &directory : settings.code_paths_) {
        std::error_code error;
        if (!std::filesystem::is_directory(directory, error)) {
            const auto bytes = directory.generic_u8string();
            outcome.diagnostics_.push_back(
                located(file, std::nullopt,
                        "parse transform path is not a directory: " + std::string(bytes.begin(), bytes.end()),
                        Severity::error, sources));
            outcome.failed_ = true;
        }
    }
    return outcome.failed_;
}

// The forms of one transform source, or false after reporting why it cannot be read. Added for transform sources
// (plan 21 LIB77C).
bool add_source(std::filesystem::path path, const TransformSettings &settings, TransformRequest &request,
                TransformOutcome &outcome, SourceManager &sources) {
    const auto known = std::ranges::any_of(request.sources_, [&](const auto &item) { return item.path_ == path; });
    if (known) {
        return true;
    }
    auto read = settings.read_source_(path);
    outcome.output_ += read.output_;
    if (!read.module_) {
        std::ranges::move(read.errors_, std::back_inserter(outcome.diagnostics_));
        const auto bytes = path.generic_u8string();
        outcome.diagnostics_.push_back(located(std::string(bytes.begin(), bytes.end()), std::nullopt,
                                               "cannot compile parse transform source; precompile it into a .beam "
                                               "with the host Erlang/OTP and pass its directory with --transform-path",
                                               Severity::error, sources));
        outcome.failed_ = true;
        return false;
    }
    request.sources_.push_back({.path_ = std::move(path), .module_ = std::move(read.module_)});
    return true;
}

// Add the project sources of the transforms that have one and no .beam on the code paths, leaving out the modules
// `host` has; false after one could not be read.
bool add_sources(const TransformSettings &settings, const HostModule &host, TransformRequest &request,
                 TransformOutcome &outcome, SourceManager &sources) {
    for (const auto &name : request.transforms_) {
        if (precompiled(name, settings.code_paths_) || !settings.find_sources_ || !settings.read_source_) {
            continue;
        }
        for (auto &path : settings.find_sources_(name, host)) {
            if (!add_source(std::move(path), settings, request, outcome, sources)) {
                return false;
            }
        }
    }
    return true;
}

// The request for one module, without its transform sources: the runner, transforms, code paths and options.
TransformRequest request_for(const std::filesystem::path &runner, std::vector<std::u32string> transforms,
                             const TransformSettings &settings, const CompileOptions &options, std::string file) {
    TransformRequest request;
    request.erl_ = runner;
    request.temporary_directory_ = settings.temporary_directory_;
    request.code_paths_ = settings.code_paths_;
    request.file_ = std::move(file);
    request.options_ = options;
    request.transforms_ = std::move(transforms);
    return request;
}

// Run the loader, or reuse its cached reply; failures that leave no reply become one error.
std::optional<TransformReply> run(const AbstractModule &module, const TransformRequest &request, HostCache *cache,
                                  TransformOutcome &outcome, SourceManager &sources) {
    try {
        return run_transforms(module, request, cache, &outcome.cached_);
    } catch (const LoaderError &error) {
        outcome.diagnostics_.push_back(located(request.file_, std::nullopt, error.what(), Severity::error, sources));
        outcome.failed_ = true;
        return std::nullopt;
    }
}

// Report an error of the module's file and fail it.
void fail(const std::string &file, std::string message, TransformOutcome &outcome, SourceManager &sources) {
    outcome.diagnostics_.push_back(located(file, std::nullopt, std::move(message), Severity::error, sources));
    outcome.failed_ = true;
}

// The host erl when there is one: none for `--erl none` or when no erl is found; an invalid --erl path is an error
// (reported, and the outcome failed).
std::optional<std::filesystem::path> host_erl(const TransformSettings &settings, const std::string &file,
                                              TransformOutcome &outcome, SourceManager &sources) {
    try {
        return find_erl(settings.erl_);
    } catch (const LoaderError &error) {
        fail(file, error.what(), outcome, sources);
        return std::nullopt;
    }
}

// The request for the host erl, with the project sources it compiles, or none after an error. Added for the
// transform host.
std::optional<TransformRequest> erl_request(const std::filesystem::path &erl, std::vector<std::u32string> transforms,
                                            const TransformSettings &settings, const CompileOptions &options,
                                            const std::string &file, TransformOutcome &outcome,
                                            SourceManager &sources) {
    // The host's own sticky modules need no sources. Added for transform source caching.
    const HostModule sticky = [&](const std::u32string &module) {
        return settings.sticky_ &&
               settings.sticky_->contains(erl, module, settings.temporary_directory_, settings.cache_.get());
    };
    auto request = request_for(erl, std::move(transforms), settings, options, file);
    if (!add_sources(settings, sticky, request, outcome, sources)) {
        return std::nullopt;
    }
    return request;
}

// Whether Clause's transform host provides a transform: Clause's library has it and no .beam on the code paths
// replaces it. Added for the transform host (plan 21 LIB77A).
bool host_provides(const std::u32string &name, const TransformSettings &settings) {
    return settings.host_ && settings.host_->provides(name) && !precompiled(name, settings.code_paths_);
}

// Whether Clause's transform host can run a transform: it provides it, or interprets a .beam on the code paths or a
// project source of it. Added for transform sources (plan 21 LIB77C).
bool host_runs(const std::u32string &name, const TransformSettings &settings) {
    if (!settings.host_ || host_provides(name, settings)) {
        return settings.host_ != nullptr;
    }
    // The host cannot interpret a module of the library it links.
    if (settings.host_->provides(name)) {
        return false;
    }
    const HostModule linked = [&](const std::u32string &module) { return settings.host_->provides(module); };
    return precompiled(name, settings.code_paths_) ||
           (settings.find_sources_ && !settings.find_sources_(name, linked).empty());
}

// The request for Clause's transform host, with the project sources it interprets, or none (after an error) when it
// cannot be built. Added for the transform host (plan 21 LIB77A).
std::optional<TransformRequest> host_request(std::vector<std::u32string> transforms, const TransformSettings &settings,
                                             const CompileOptions &options, const std::string &file,
                                             TransformOutcome &outcome, SourceManager &sources) {
    std::filesystem::path host;
    try {
        host = settings.host_->executable();
    } catch (const LoaderError &error) {
        fail(file, error.what(), outcome, sources);
        return std::nullopt;
    }
    auto request = request_for(host, std::move(transforms), settings, options, file);
    request.clause_host_ = true;
    const HostModule linked = [&](const std::u32string &module) { return settings.host_->provides(module); };
    if (!add_sources(settings, linked, request, outcome, sources)) {
        return std::nullopt;
    }
    return request;
}

// The request of a module's transforms: Clause's transform host when the library provides them all, else the host
// erl; without one, the host also interprets project sources and .beam files (plan 21 LIB77C).
std::optional<TransformRequest> request_of(std::vector<std::u32string> transforms, const TransformSettings &settings,
                                           const CompileOptions &options, const std::string &file,
                                           TransformOutcome &outcome, SourceManager &sources) {
    const auto provided = [&](const std::u32string &name) { return host_provides(name, settings); };
    if (std::ranges::all_of(transforms, provided)) {
        return host_request(std::move(transforms), settings, options, file, outcome, sources);
    }
    const auto erl = host_erl(settings, file, outcome, sources);
    if (erl || outcome.failed_) {
        return erl ? erl_request(*erl, std::move(transforms), settings, options, file, outcome, sources) : std::nullopt;
    }
    const auto missing =
        std::ranges::find_if_not(transforms, [&](const auto &name) { return host_runs(name, settings); });
    if (missing != transforms.end()) {
        fail(file, "parse transform '" + utf8(*missing) + "' needs Erlang/OTP 29 on the host; pass --erl", outcome,
             sources);
        return std::nullopt;
    }
    return host_request(std::move(transforms), settings, options, file, outcome, sources);
}
} // namespace

bool wants_transforms(const ast::Module &syntax, const TransformSettings &settings) {
    return !settings.transforms_.empty() || std::ranges::any_of(syntax.forms(), [&](const ast::FormId &id) {
        return names_transform(syntax, syntax.form(id));
    });
}

TransformOutcome transform_module(const ast::Module &syntax, const Source &main, const TransformSettings &settings,
                                  const CompileOptions &options, const FeatureSnapshot &features,
                                  const ParserSession &parser) {
    TransformOutcome outcome;
    SourceManager sources;
    outcome.end_ = end_of(syntax, syntax.forms(), main);
    auto module = export_module(syntax, syntax.forms(), outcome.end_);
    auto transforms = transform_names(module, settings);
    remove_transform_options(module);
    if (missing_paths(settings, main.name, outcome, sources)) {
        return outcome;
    }
    const auto request = request_of(std::move(transforms), settings, options, main.name, outcome, sources);
    if (!request) {
        return outcome;
    }
    const auto reply = run(module, *request, settings.cache_.get(), outcome, sources);
    if (!reply) {
        return outcome;
    }
    outcome.output_ = reply->output_;
    outcome.reply_ = reply->bytes_;
    report(*reply, outcome, sources);
    if (!reply->ok_) {
        outcome.failed_ = true;
        return outcome;
    }
    const ImportOptions import{
        .main_file_ = main.name,
        .source_ = [&sources](const std::string &file) { return existing_source(sources, file); },
        .features_ = features};
    auto imported = import_forms(reply->terms_, reply->forms_, import, parser);
    outcome.failed_ = std::ranges::any_of(imported.diagnostics_,
                                          [](const Diagnostic &item) { return item.severity == Severity::error; });
    std::ranges::move(imported.diagnostics_, std::back_inserter(outcome.diagnostics_));
    outcome.end_ = imported.eof_.value_or(outcome.end_);
    return outcome;
}
} // namespace clause::transforms
