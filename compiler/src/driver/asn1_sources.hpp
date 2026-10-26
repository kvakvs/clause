#pragma once
// Added for ASN.1 sources (plan 21 step LIB84): clau compiles .asn, .asn1 and .set.asn files as asn1ct does, writing
// Module.erl and Module.hrl (docs/asn1.md), and compiles the generated modules with the batch.
#include <clause/compiler/diagnostic.hpp>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace clause::cli {
struct Asn1Settings {
    // asn1ct options as spelled (`per`, `der`, `no_ok_wrapper`, ...).
    std::vector<std::string> options_;
    // Where Module.erl and Module.hrl are written; asn1ct's outdir.
    std::filesystem::path outdir_;
    // Where imported ASN.1 modules are looked for after the source's own directory; asn1ct's `i` option.
    std::vector<std::filesystem::path> include_dirs_;
};

// True for a path clau compiles as an ASN.1 specification.
bool asn1_source(const std::filesystem::path &path);

// Check the options before any source is read; returns a message for an unknown or unsupported one.
std::optional<std::string> check_asn1_options(const std::vector<std::string> &options);

// Compile each source; returns the generated .erl files, or nothing after reporting a diagnostic.
std::optional<std::vector<std::filesystem::path>> generate_asn1(const std::vector<std::filesystem::path> &sources,
                                                                const Asn1Settings &settings,
                                                                const DiagnosticSink &sink);
} // namespace clause::cli
