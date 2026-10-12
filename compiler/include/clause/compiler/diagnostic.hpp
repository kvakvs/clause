#pragma once
#include <clause/compiler/source.hpp>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>

namespace clause {
struct Span {
    // Share source ownership; offsets index decoded characters, not bytes.
    SourcePtr source;
    std::size_t begin = 0;
    std::size_t end = 0;
};
enum class DiagnosticCode : std::uint8_t {
    invalid_character,
    invalid_number,
    invalid_escape,
    unterminated_literal,
    string_indentation,
    adjacent_strings,
    malformed_directive,
    missing_terminator,
    misplaced_directive,
    resource_limit,
    undefined_macro,
    macro_redefinition,
    macro_arguments,
    macro_cycle,
    conditional_structure,
    invalid_condition,
    include_not_found,
    invalid_context,
    invalid_feature,
    user_error,
    user_warning,
    invalid_encoding,
    parser_syntax,
    unsupported_syntax,
    parser_contract,
    duplicate_application
};

enum class Severity : std::uint8_t { error, warning };

struct LogicalLocation {
    // Preserve logical coordinates separately from the physical spelling span.
    std::string file;
    std::size_t line;
    std::size_t column;
};

// Structured result data; use DiagnosticError when unwinding a failed parse.
struct Diagnostic {
    // Keep stable machine-readable identity separately from presentation.
    DiagnosticCode code;
    std::string message;
    // Retain the primary spelling and related expansion/include locations.
    Span primary;
    std::vector<Span> related;
    // Warnings remain observable without marking a module unsuccessful.
    Severity severity = Severity::error;
    // Render logical coordinates while retaining physical spelling and provenance.
    std::optional<LogicalLocation> location{};
    // Parser expectations and the nearest unmatched opener are structured, optional context.
    std::string expected{};
    std::optional<LogicalLocation> opener{};
};

class DiagnosticError : public std::runtime_error {
  public:
    // Transport a structured diagnostic through standard exception handlers.
    explicit DiagnosticError(Diagnostic diagnostic);
    // Allow parser recovery to attach expectations and unmatched opener locations.
    Diagnostic diagnostic;
};

// Allow callers to attach project context without changing frontend source rendering.
using DiagnosticSink = std::function<void(std::string_view)>;

class LexicalError : public std::runtime_error {
  public:
    // Transport a source diagnostic from a lexical helper to the form driver.
    explicit LexicalError(Diagnostic diagnostic);
    const Diagnostic diagnostic;
};

// Render logical coordinates and related physical expansion/include sites.
std::string render(const Diagnostic &diagnostic);
} // namespace clause
