#pragma once
#include "expression.hpp"
#include "macros.hpp"

namespace clause {
struct PreprocessorSession::State {
    struct Branch {
        // Branch activity remains local to an include file; its opener survives errors.
        Token opening;
        bool parent = true;
        bool selected = false;
        bool active = false;
        bool seen_else = false;
    };

    struct File {
        // Physical path, logical scanner coordinates, and conditional state restore on return.
        SourcePtr source;
        Lexer lexer;
        std::filesystem::path path;
        std::string logical_name;
        std::vector<Branch> branches;
        std::optional<Token> include_site;
        std::size_t resume_line = 1;
    };

    enum class FeatureLifecycle : std::uint8_t { experimental, approved, permanent, rejected };

    struct Feature {
        // Pinned lifecycle metadata is distinct from the module's mutable enabled flag.
        FeatureLifecycle lifecycle;
        bool enabled;
    };

    // Every semantic resource belongs to this single module session.
    PreprocessorOptions options;
    SourceManager sources;
    MacroTable macros;
    std::vector<File> files;
    std::map<std::string, Feature> features{{"maybe_expr", {FeatureLifecycle::approved, true}},
                                            {"compr_assign", {FeatureLifecycle::experimental, false}}};
    std::vector<std::string> enabled_features{"maybe_expr"};
    // Emitted forms share immutable state until a feature directive changes it.
    FeatureSnapshot feature_snapshot = std::make_shared<const FeatureContext>(FeatureContext{enabled_features});
    std::optional<Token> module;
    std::optional<Token> base_module;
    bool prefix = true;
    bool failed = false;
    std::deque<PreprocessorEvent> pending;

    // Initialize defaults and predefinitions without leaking errors as host exceptions.
    State(const SourcePtr &source, PreprocessorOptions settings);
    void initialize();
    void initial_feature(const std::string &name, bool enabled);
    void predefine(std::string_view definition);
    void diagnostic(Diagnostic value);
    // Stream one physical form at a time and apply only active directives.
    std::optional<PreprocessorEvent> next();
    void scan();
    void process(std::vector<Token> tokens);
    void apply(Directive directive, const Token &site);
    bool active() const;
    bool conditional(DirectiveKind kind, std::span<const Token> tokens);
    void begin_branch(DirectiveKind kind, std::span<const Token> tokens);
    void select_branch(DirectiveKind kind, std::span<const Token> tokens);
    void change_branch(DirectiveKind kind, std::span<const Token> tokens);
    bool test_branch(const Directive &directive);
    void end_file();
    // Include resolution has injectable filesystem/environment access and bounded nesting.
    void include(const Directive &directive, const Token &site);
    // The directories an include name is searched below: the including file's, the working one, include paths.
    std::vector<std::filesystem::path> search_bases() const;
    std::vector<std::filesystem::path> candidates(const std::string &name, bool library) const;
    // Retry an include_lib name below the newest `<application>-<version>` directory of each search base, as a
    // release names them. Added for versioned include_lib directories.
    bool open_versioned(const std::filesystem::path &name, const Token &site);
    // Warn that several versions of one application directory exist. Added for versioned include_lib directories.
    void report_duplicates(const ApplicationVersions::Match &match, const Token &site);
    // Enter one include candidate when the lookup hook allows it and it can be read; false to try the next one.
    bool open_include(const std::filesystem::path &path, const Token &site);
    std::string expand_environment(std::string name) const;
    void push_file(const SourcePtr &source, std::filesystem::path path, const Token &site);
    // Dynamic builtins retain invocation locations; function context is resolved in two passes.
    std::optional<std::vector<Token>> module_builtin(const Token &name);
    std::optional<std::vector<Token>> builtin(const Token &name);
    std::vector<Token> expand(std::span<const Token> tokens, bool function = false);
    void function_context(std::vector<Token> &tokens);
    void module_context(const std::vector<Token> &tokens);
    bool attribute(const std::vector<Token> &tokens);
    void ordinary(std::vector<Token> tokens);
    void file_mapping(const std::vector<Token> &tokens);
    void emit_file(const Token &site, const std::string &name, std::size_t line);
    // Release-specific feature catalog controls keyword lexing and feature macros.
    void feature(const std::string &name, bool enabled, const Token &site);
    void feature_macros();
    void keywords();
};
} // namespace clause
