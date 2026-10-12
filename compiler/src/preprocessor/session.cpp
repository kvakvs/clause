#include "engine.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <memory>

namespace clause {
namespace {
// Read optional includes without conflating an empty file with a missing path.
std::optional<std::string> read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// Copy environment values into session-owned storage using the platform CRT API.
std::optional<std::string> environment(const std::string_view name) {
#ifdef _WIN32
    char *buffer = nullptr;
    std::size_t size = 0;
    const auto error = _dupenv_s(&buffer, &size, std::string(name).c_str());
    const std::unique_ptr<char, decltype(&std::free)> value(buffer, &std::free);
    if (error != 0 || !value) {
        return std::nullopt;
    }
    return std::string(value.get());
#else
    const auto *value = std::getenv(std::string(name).c_str());
    return value ? std::optional<std::string>(value) : std::nullopt;
#endif
}

// Preserve token source/location ownership for EOF and initialization errors.
Token start_token(const SourcePtr &source) {
    return {TokenKind::atom, std::u32string{}, {source, 0, 0}, {source->name, 1, 1}, {}};
}

// OTP consumes one whitespace character after a form dot; only LF advances the return line.
std::size_t form_resume_line(const Token &dot) {
    const auto &span = dot.spelling;
    const auto &text = span.source->text;
    const bool newline = span.end < text.size() && text[span.end] == U'\n';
    return dot.location.line + static_cast<std::size_t>(newline);
}
} // namespace

PreprocessorSession::State::State(const SourcePtr &source, PreprocessorOptions settings)
    : options(std::move(settings)) {
    if (options.working_directory.empty()) {
        options.working_directory = std::filesystem::current_path();
    }
    if (!options.read_file) {
        options.read_file = read_file;
    }
    if (!options.environment) {
        options.environment = environment;
    }
    if (!options.application_versions) {
        options.application_versions = std::make_shared<ApplicationVersions>();
    }
    files.push_back({source, Lexer(source), source->name, source->name, {}, {}, 1});
    initialize();
    emit_file(start_token(source), source->name, 1);
}

void PreprocessorSession::State::initial_feature(const std::string &name, const bool enabled) {
    if (name == "all") {
        for (const auto &[known, state] : features) {
            (void)state;
            feature(known, enabled, start_token(files.front().source));
        }
    } else {
        feature(name, enabled, start_token(files.front().source));
    }
}

void PreprocessorSession::State::initialize() {
    macros.reserved = {U"FILE",    U"LINE",          U"FUNCTION_NAME", U"FUNCTION_ARITY",
                       U"MODULE",  U"MODULE_STRING", U"BASE_MODULE",   U"BASE_MODULE_STRING",
                       U"MACHINE", U"BEAM",          U"OTP_RELEASE"};
    macros.undefined = {U"MODULE",        U"MODULE_STRING", U"BASE_MODULE", U"BASE_MODULE_STRING",
                        U"FUNCTION_NAME", U"FUNCTION_ARITY"};
    feature_macros();
    for (const auto &[name, enabled] : options.features) {
        try {
            initial_feature(name, enabled);
        } catch (const DiagnosticError &error) {
            diagnostic(error.diagnostic);
        }
    }
    for (const auto &definition : options.definitions) {
        try {
            predefine(definition);
        } catch (const DiagnosticError &error) {
            diagnostic(error.diagnostic);
        } catch (const LexicalError &error) {
            diagnostic(error.diagnostic);
        }
    }
}

void PreprocessorSession::State::predefine(std::string_view definition) {
    const auto equals = definition.find('=');
    const auto name = fragment(std::string(definition.substr(0, equals)));
    if (name.size() != 1 || (name[0].kind != TokenKind::atom && name[0].kind != TokenKind::variable)) {
        pp_fail(DiagnosticCode::macro_arguments, "invalid initial macro name", start_token(files.front().source));
    }
    auto body = fragment(equals == std::string_view::npos ? "true" : std::string(definition.substr(equals + 1)));
    if (body.empty()) {
        pp_fail(DiagnosticCode::macro_arguments, "expected initial macro term", name.front());
    }
    const auto value = parse_term(body, options.limits.expression_depth);
    macros.define({name.front(), std::nullopt, term_tokens(value, name.front())});
}

void PreprocessorSession::State::diagnostic(Diagnostic value) {
    if (value.severity == Severity::error) {
        failed = true;
    }
    for (const auto &file : files) {
        if (!value.location && file.source == value.primary.source) {
            value.location = file.lexer.logical_location(value.primary.begin);
        }
        if (file.include_site) {
            value.related.push_back(file.include_site->spelling);
        }
    }
    pending.emplace_back(std::move(value));
}

bool PreprocessorSession::State::active() const {
    return files.back().branches.empty() || files.back().branches.back().active;
}

std::optional<PreprocessorEvent> PreprocessorSession::State::next() {
    while (pending.empty() && !files.empty()) {
        scan();
    }
    if (pending.empty()) {
        return std::nullopt;
    }
    auto result = std::move(pending.front());
    pending.pop_front();
    return result;
}

void PreprocessorSession::State::scan() {
    keywords();
    try {
        auto tokens = files.back().lexer.form();
        if (tokens.empty()) {
            end_file();
            return;
        }
        if (tokens.back().kind != TokenKind::dot) {
            pp_fail(DiagnosticCode::missing_terminator, "expected form-ending '.'", tokens.back());
        }
        files.back().resume_line = form_resume_line(tokens.back());
        process(std::move(tokens));
    } catch (const LexicalError &error) {
        files.back().lexer.recover_form();
        if (active()) {
            diagnostic(error.diagnostic);
        }
    } catch (const DiagnosticError &error) {
        diagnostic(error.diagnostic);
    }
}

void PreprocessorSession::State::process(std::vector<Token> tokens) {
    const auto kind = directive_kind(tokens);
    if (kind && conditional(*kind, tokens)) {
        return;
    }
    if (!active()) {
        return;
    }
    if (!kind) {
        ordinary(expand(tokens, true));
        return;
    }
    auto parsed = parse_directive(tokens);
    if (auto *error = std::get_if<Diagnostic>(&parsed)) {
        throw DiagnosticError(std::move(*error));
    }
    apply(std::get<Directive>(std::move(parsed)), tokens[1]);
}

void PreprocessorSession::State::apply(Directive directive, const Token &site) {
    switch (directive.kind) {
    case DirectiveKind::define:
        macros.define(std::get<Definition>(std::move(directive.operand)));
        return;
    case DirectiveKind::undef:
        prefix = false;
        macros.undefine(std::get<MacroName>(directive.operand).name.text());
        return;
    case DirectiveKind::include:
    case DirectiveKind::include_lib:
        include(directive, site);
        return;
    case DirectiveKind::feature: {
        const auto &setting = std::get<FeatureSetting>(directive.operand);
        feature(utf8(setting.name.text()), setting.enabled, site);
        return;
    }
    default:
        break;
    }
    prefix = false;
    const auto tokens = expand(std::get<TokenOperand>(directive.operand).tokens);
    if (tokens.empty()) {
        pp_fail(DiagnosticCode::malformed_directive, "expected diagnostic term", site);
    }
    const auto value = parse_term(tokens, options.limits.expression_depth);
    const bool warning = directive.kind == DirectiveKind::warning;
    diagnostic({warning ? DiagnosticCode::user_warning : DiagnosticCode::user_error, display(value), site.spelling,
                site.origins, warning ? Severity::warning : Severity::error, site.location});
}

void PreprocessorSession::State::end_file() {
    if (!files.back().branches.empty()) {
        const auto branch = files.back().branches.back();
        files.back().branches.pop_back();
        pp_fail(DiagnosticCode::conditional_structure, "unterminated conditional", branch.opening);
    }
    files.pop_back();
    if (!files.empty()) {
        const auto &file = files.back();
        emit_file(start_token(file.source), file.logical_name, file.resume_line);
    }
}

PreprocessorSession::PreprocessorSession(const SourcePtr &source, PreprocessorOptions options)
    : state_(std::make_unique<State>(source, std::move(options))) {}

PreprocessorSession::~PreprocessorSession() = default;

bool PreprocessorSession::failed() const { return state_->failed; }

FeatureSnapshot PreprocessorSession::features() const { return state_->feature_snapshot; }

std::optional<PreprocessorEvent> PreprocessorSession::next() { return state_->next(); }
} // namespace clause
