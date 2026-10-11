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

// The request for one module: its transforms, project sources of those that have one, the host erl.
TransformRequest request_for(const std::filesystem::path &erl, std::vector<std::u32string> transforms,
                             const TransformSettings &settings, const CompileOptions &options, std::string file) {
    TransformRequest request;
    request.erl_ = erl;
    request.temporary_directory_ = settings.temporary_directory_;
    request.code_paths_ = settings.code_paths_;
    request.file_ = std::move(file);
    request.options_ = options;
    // Transform sources compile with the module's include directories and defines, never with transforms.
    auto source_options = options;
    source_options.features_.clear();
    source_options.transforms_.clear();
    // The host's own sticky modules need no sources. Added for transform source caching.
    const HostModule host = [&](const std::u32string &module) {
        return settings.sticky_ &&
               settings.sticky_->contains(erl, module, settings.temporary_directory_, settings.cache_.get());
    };
    for (const auto &name : transforms) {
        if (precompiled(name, settings.code_paths_) || !settings.find_sources_) {
            continue;
        }
        for (auto &path : settings.find_sources_(name, host)) {
            const auto known =
                std::ranges::any_of(request.sources_, [&](const auto &item) { return item.path_ == path; });
            if (!known) {
                request.sources_.push_back({.path_ = std::move(path), .options_ = source_options});
            }
        }
    }
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

// The host erl, or an error naming the first transform when there is none.
std::optional<std::filesystem::path> host_erl(const TransformSettings &settings, const std::u32string &transform,
                                              const std::string &file, TransformOutcome &outcome,
                                              SourceManager &sources) {
    std::string problem;
    try {
        if (auto erl = find_erl(settings.erl_)) {
            return erl;
        }
        problem = "parse transform '" + utf8(transform) + "' needs Erlang/OTP 29 on the host; pass --erl";
    } catch (const LoaderError &error) {
        problem = error.what();
    }
    outcome.diagnostics_.push_back(located(file, std::nullopt, problem, Severity::error, sources));
    outcome.failed_ = true;
    return std::nullopt;
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
    const auto erl = host_erl(settings, transforms.front(), main.name, outcome, sources);
    if (!erl) {
        return outcome;
    }
    const auto reply = run(module, request_for(*erl, std::move(transforms), settings, options, main.name),
                           settings.cache_.get(), outcome, sources);
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
