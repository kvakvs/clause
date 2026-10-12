#include "analysis.hpp"
#include "../codegen/limits.hpp"
#include "../codegen/progress.hpp"
#include "../project/paths.hpp"
#include "../semantic/behaviours.hpp"
#include "../semantic/bindings.hpp"
#include "../semantic/capabilities.hpp"
#include "../semantic/liveness.hpp"
#include "../semantic/services.hpp"

namespace clause::cli {
namespace {
// Establish all owned declaration tables before resolving inter-module references.
void index_inputs(const codegen::CompilationRequest &request, Analysis &analysis, const semantic::Reporter &report) {
    analysis.modules.reserve(request.inputs.size());
    for (const auto &input : request.inputs) {
        codegen::progress(request, "analysis", input.source_path);
        auto module =
            semantic::index(input.syntax, project::path_text(input.source_path), report,
                            {.escript_ = input.escript, .predefined_ = input.predefined_, .library_ = input.library_});
        semantic::bind_parameters(*module, report);
        semantic::resolve_services(*module, report);
        semantic::check_capabilities(*module, report);
        analysis.modules.push_back(std::move(module));
    }
    semantic::check_behaviours(analysis.modules, report);
}

// Run one analysis phase, timed under --print-stage-times.
template <typename Work> void phase(Analysis &analysis, const std::string_view name, Work &&work) {
    const auto lap = analysis.times.lap(name);
    std::forward<Work>(work)();
}

// Attach known module identities to each batch analysis phase before invoking its shared implementation.
void trace_analysis(const std::string_view phase, const codegen::CompilationRequest &request,
                    const Analysis &analysis) {
    for (std::size_t i = 0; i < analysis.modules.size(); ++i) {
        codegen::progress(request, phase, request.inputs[i].source_path, utf8(analysis.modules[i]->name));
    }
}
} // namespace

bool analyze(const codegen::CompilationRequest &request, const EntryRequest &entry, Analysis &analysis,
             const DiagnosticSink &sink) {
    codegen::validate_input_limits(request.inputs, request.limits);
    bool failed = false;
    const semantic::Reporter report = [&](const Diagnostic &diagnostic) {
        failed = failed || diagnostic.severity == Severity::error;
        const auto prefix = diagnostic.severity == Severity::warning ? "warning: " : "error: ";
        sink(prefix + render(diagnostic));
    };
    bool entry_failed = false;
    phase(analysis, "index", [&] {
        index_inputs(request, analysis, report);
        entry_failed = resolve_entry(analysis.modules, entry, report, sink, analysis.entry);
    });
    if (failed || entry_failed) {
        return false;
    }
    trace_analysis("calls", request, analysis);
    phase(analysis, "calls", [&] { analysis.calls = semantic::resolve_calls(analysis.modules, report); });
    if (failed) {
        return false;
    }
    trace_analysis("declared-types", request, analysis);
    phase(analysis, "declared-types",
          [&] { analysis.declared = semantic::types::resolve_declarations(analysis.modules, report); });
    if (failed) {
        return false;
    }
    trace_analysis("inference", request, analysis);
    auto *profile = analysis.times.enabled() ? &analysis.profile : nullptr;
    phase(analysis, "inference", [&] { analysis.inferred = semantic::types::infer(analysis.calls, {}, profile); });
    trace_analysis("contracts", request, analysis);
    phase(analysis, "contracts",
          [&] { semantic::types::check_contracts(*analysis.declared, *analysis.inferred, analysis.calls, report); });
    return !failed;
}

void drop_unreachable(const codegen::CompilationRequest &request, Analysis &analysis) {
    const auto &entry = *analysis.entry;
    auto &module = *analysis.modules.at(entry.module);
    auto &function = module.functions.at(module.lookup.at(entry.function));
    const auto report = semantic::mark_live(analysis.modules, analysis.calls, {&module, &function});
    for (std::size_t i = 0; i < analysis.modules.size(); ++i) {
        const auto &name = analysis.modules[i]->name;
        const auto kept = report.kept_.find(name);
        codegen::progress(request, "liveness", request.inputs[i].source_path, utf8(name),
                          kept != report.kept_.end() ? "kept-all-exports reason=" + kept->second
                                                     : "dropped-exports=" + std::to_string(report.dropped_.at(name)));
    }
}
} // namespace clause::cli
