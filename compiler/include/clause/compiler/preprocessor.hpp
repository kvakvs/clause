#pragma once
#include <clause/compiler/directive.hpp>
#include <clause/compiler/features.hpp>
#include <functional>
#include <map>

namespace clause {
struct MacroKey {
    // Keep object form distinct from every parameter arity, including zero.
    std::u32string name;
    std::optional<std::size_t> arity;
    auto operator<=>(const MacroKey &) const = default;
};

struct OrdinaryForm {
    // Pass expanded tokens to parsing; DirectiveReader alone returns unexpanded tokens.
    std::vector<Token> tokens;
    // Keep immutable feature state at emission time; syntax-only readers leave it unspecified.
    FeatureSnapshot features{};
};

using PreprocessorEvent = std::variant<OrdinaryForm, Directive, Diagnostic>;

class DirectiveReader {
  public:
    // Start an isolated module; this foundation parses syntax without applying
    // directives.
    explicit DirectiveReader(SourcePtr source);
    // Return one form/directive/error, or EOF; errors never reset failure
    // status.
    std::optional<PreprocessorEvent> next();
    bool failed() const;

  private:
    struct IncludeFrame {
        // Keep each physical source and its incremental scanner alive across
        // forms.
        SourcePtr source;
        Lexer lexer;
    };

    // Syntax reading needs only its owned scanner; semantic state belongs to PreprocessorSession.
    std::vector<IncludeFrame> includes_;
    // Latch errors independently of the event stream consumed by the caller.
    bool failed_ = false;

    // Classify a complete lexical form and report misplaced directive syntax.
    PreprocessorEvent classify(std::vector<Token> tokens);
    // Latch failure before returning any diagnostic event.
    PreprocessorEvent error(Diagnostic diagnostic);
};

struct PreprocessorLimits {
    // Bound expansion work and memory independently of language validity.
    std::size_t expansion_depth = 256;
    std::size_t tokens = 1000000;
    std::size_t include_depth = 64;
    std::size_t expression_depth = 256;
};

struct PreprocessorOptions {
    // Resolve host paths independently of the executable's eventual target.
    std::filesystem::path working_directory;
    std::vector<std::filesystem::path> include_paths;
    // Ordered application overrides use the last matching entry; moving options cannot allocate.
    std::vector<std::pair<std::string, std::filesystem::path>> applications;
    // Initial macro names or NAME=TERM values and ordered feature changes.
    std::vector<std::string> definitions;
    std::vector<std::pair<std::string, bool>> features;
    PreprocessorLimits limits;
    // Tests/embedders can replace filesystem and environment access.
    std::function<std::optional<std::string>(const std::filesystem::path &)> read_file;
    // Lookup hook (the driver's directory index, plan step 62E): false when an include candidate certainly does not
    // exist, so it is not opened. Without it every candidate is opened in turn.
    std::function<bool(const std::filesystem::path &)> may_exist;
    std::function<std::optional<std::string>(std::string_view)> environment;
    // Observe each successfully decoded include synchronously, before its forms are processed.
    std::function<void(const std::filesystem::path &)> include_loaded;
};

class PreprocessorSession {
  public:
    // Create an isolated semantic session; directives take effect in source order.
    explicit PreprocessorSession(const SourcePtr &source, PreprocessorOptions options = {});
    ~PreprocessorSession();
    PreprocessorSession(const PreprocessorSession &) = delete;
    PreprocessorSession &operator=(const PreprocessorSession &) = delete;
    // Return expanded ordinary forms or diagnostics; EOF never clears prior errors.
    std::optional<PreprocessorEvent> next();
    bool failed() const;
    // Return current feature state, including final module state after EOF.
    FeatureSnapshot features() const;

  private:
    struct State;
    // Keep semantic implementation and Boost types outside public compiler headers.
    std::unique_ptr<State> state_;
};
} // namespace clause
