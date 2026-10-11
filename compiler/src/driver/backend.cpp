#include "backend.hpp"
#include "../codegen/emission.hpp"
#include "../codegen/frames.hpp"
#include "../codegen/lowering.hpp"
#include "../codegen/optimization.hpp"
#include "../codegen/serialization.hpp"
#include "../codegen/target.hpp"
#include "../linking/link.hpp"
#include "../project/paths.hpp"
#include "analysis.hpp"
#include "compile_cache.hpp"
#include "inspection.hpp"
#include "progress.hpp"
#include "publication.hpp"
#include "stage_times.hpp"
#include "type_report.hpp"
#include <clause/compiler/source.hpp>

namespace clause::cli {
namespace {
// Carry invocation policy into the private backend without exposing LLVM types to the driver.
codegen::CompilationRequest backend_request(std::vector<codegen::CompilationInput> inputs,
                                            const FrontendRequest &frontend) {
    codegen::CompilationRequest request;
    request.inputs = std::move(inputs);
    request.project_target = frontend.project_target;
    request.progress = progress_callback(frontend);
    const auto &options = frontend.backend;
    request.target_triple = options.target_triple;
    request.optimization = options.optimization.value_or(codegen::OptimizationLevel::none);
    request.disable_type_specialization = options.disable_type_specialization;
    // Link-time optimization links bitcode instead of objects; LLD optimizes it together at link.
    request.output_kind =
        options.lto ? codegen::OutputKind::llvm_bitcode : options.emit.value_or(codegen::OutputKind::object);
    request.debug_info = options.debug_info;
    request.annotate_source =
        options.inspect_ir() || request.output_kind == codegen::OutputKind::llvm_ir || request.debug_info;
    return request;
}

// Preserve severity when presenting owned backend messages.
std::string_view diagnostic_prefix(const codegen::DiagnosticLevel level) {
    switch (level) {
    case codegen::DiagnosticLevel::error:
        return "error: ";
    case codegen::DiagnosticLevel::warning:
        return "warning: ";
    case codegen::DiagnosticLevel::note:
        return "note: ";
    }
    return "error: ";
}

// Deliver owned backend diagnostics exactly once through the same positional/project sink.
void report_backend(const codegen::CompilationResult &result, const DiagnosticSink &sink) {
    for (const auto &diagnostic : result.diagnostics()) {
        if (!diagnostic.reported) {
            sink(std::string(diagnostic_prefix(diagnostic.level)) + diagnostic.message);
        }
    }
    if (result.diagnostic_capture_failed()) {
        sink("error: backend diagnostic capture failed");
    }
}

// Keep object/IR serialization in memory until every module has successfully completed its phases.
bool emit(codegen::Compilation &compilation) {
    const auto kind = compilation.request().output_kind;
    return kind == codegen::OutputKind::object ? codegen::emit_objects(compilation)
                                               : codegen::emit_ir(compilation, kind);
}

// The outputs a fresh compilation leaves, with what linking and publication need besides them.
Compiled compiled_of(codegen::Compilation compilation) {
    Compiled result;
    result.triple = codegen::target_triple(compilation);
    result.object_extension = codegen::object_extension(compilation);
    for (const auto &input : compilation.request().inputs) {
        result.inputs.push_back(input.source_path);
    }
    result.result = std::move(compilation).take_result();
    return result;
}

// Queue complete project batches or publish a successful positional invocation immediately.
void deliver(Compiled compiled, const FrontendRequest &frontend) {
    if (!frontend.backend.emit) {
        return;
    }
    auto inputs = frontend.protected_inputs;
    inputs.insert(inputs.end(), compiled.inputs.begin(), compiled.inputs.end());
    Publication pending{std::move(compiled.result), frontend.backend.artifact_directory.value_or("build/aot"),
                        std::move(inputs), compiled.object_extension, frontend.project_target};
    if (frontend.pending_publications) {
        frontend.pending_publications->push_back(std::move(pending));
    } else {
        publish(pending);
    }
}

// Share lowering and specialization, then stop at the selected inspection or artifact boundary.
bool generate(codegen::Compilation &compilation, const Analysis &analysis, const FrontendRequest &frontend) {
    if (!codegen::lower(compilation, analysis.modules, *analysis.inferred) || !codegen::lower_frames(compilation)) {
        return false;
    }
    if (frontend.backend.inspect_ir()) {
        return inspect_ir(compilation, frontend);
    }
    return codegen::optimize(compilation) && emit(compilation);
}

// Link the in-memory module and startup objects into `output`; project executables wait for the whole invocation.
void link(const Compiled &compiled, const FrontendRequest &frontend, const std::filesystem::path &output,
          const DiagnosticSink &sink) {
    auto inputs = frontend.protected_inputs;
    inputs.insert(inputs.end(), compiled.inputs.begin(), compiled.inputs.end());
    StageClock clock; // Added for --print-stage-times.
    auto executable = clock.time([&] {
        return linking::stage_executable(
            {.output = output,
             .target_triple = compiled.triple,
             .objects = compiled.result.outputs(),
             .linker = frontend.backend.linker,
             .runtime_library = frontend.backend.runtime_library,
             .protected_inputs = inputs,
             .create_directory = frontend.create_output_directory,
             .strip_unused = frontend.backend.optimization == codegen::OptimizationLevel::size,
             .debug_info = frontend.backend.debug_info,
             .lto = frontend.backend.lto});
    });
    if (frontend.stage_times) {
        print_stage_time("link", project::path_text(executable.output), clock.elapsed());
    }
    if (!executable.warnings.empty()) {
        sink(executable.warnings);
    }
    if (frontend.pending_executables) {
        frontend.pending_executables->push_back({frontend.project_target, std::move(executable)});
    } else {
        linking::publish_executable(executable);
    }
}

// Select the startup module from the resolved entry, if any.
std::optional<codegen::StartupRequest> startup_request(const Analysis &analysis) {
    if (!analysis.entry) {
        return std::nullopt;
    }
    const auto &entry = *analysis.entry;
    return codegen::StartupRequest{entry.module, utf8(entry.function.name), entry.function.arity, entry.escript};
}

// Analyze before constructing LLVM state; moving the vector preserves borrowed AST addresses. A successful batch
// leaves its compilation in `output` unless it stopped at --print-types.
bool compile(std::vector<codegen::CompilationInput> inputs, const FrontendRequest &frontend, const DiagnosticSink &sink,
             std::optional<codegen::Compilation> &output) {
    auto request = backend_request(std::move(inputs), frontend);
    Analysis analysis;
    const EntryRequest entry{frontend.entry, frontend.executable_output.has_value(), !request.project_target.empty()};
    if (!analyze(request, entry, analysis, sink)) {
        return true;
    }
    if (frontend.backend.print_types) {
        print_types(analysis, request);
        return false;
    }
    if (frontend.executable_output && !frontend.backend.keep_exports) {
        drop_unreachable(request, analysis);
    }
    request.startup = startup_request(analysis);
    auto &compilation = output.emplace(std::move(request));
    const bool succeeded = generate(compilation, analysis, frontend);
    report_backend(compilation.result(), sink);
    return !succeeded || !compilation.result().complete();
}

// The batch a compile time belongs to: its module count, and its project target when there is one.
// Added for --print-stage-times.
std::string batch_text(const std::size_t modules, const FrontendRequest &frontend) {
    auto text = std::to_string(modules) + (modules == 1 ? " module" : " modules");
    return frontend.project_target.empty() ? text : "target " + frontend.project_target + " (" + text + ")";
}

// Compile the batch; a successful one leaves its outputs in `compiled`.
bool compile_fresh(std::vector<codegen::CompilationInput> inputs, const FrontendRequest &frontend,
                   const DiagnosticSink &sink, std::optional<Compiled> &compiled) {
    std::optional<codegen::Compilation> compilation;
    const bool failed = compile(std::move(inputs), frontend, sink, compilation);
    if (!failed && compilation) {
        compiled = compiled_of(std::move(*compilation));
    }
    return failed;
}

// Compile the batch, or replay the outcome the compile cache keeps for the same inputs and options (`cached` is
// then set); a fresh outcome is kept with its diagnostics. Added for the compile cache (62D4).
bool compile_or_reuse(std::vector<codegen::CompilationInput> inputs, const FrontendRequest &frontend,
                      const DiagnosticSink &sink, std::optional<Compiled> &compiled, bool &cached) {
    const auto cache = compile_cache(inputs, frontend);
    if (!cache) {
        return compile_fresh(std::move(inputs), frontend, sink, compiled);
    }
    std::vector<std::filesystem::path> paths;
    paths.reserve(inputs.size());
    for (const auto &input : inputs) {
        paths.push_back(input.source_path);
    }
    if (auto stored = load_outcome(*cache, paths)) {
        for (const auto &text : stored->diagnostics) {
            sink(text);
        }
        compiled = std::move(stored->compiled);
        cached = true;
        return stored->failed;
    }
    CompileOutcome outcome;
    const DiagnosticSink recording = [&](const std::string_view message) {
        outcome.diagnostics.emplace_back(message);
        sink(message);
    };
    outcome.failed = compile_fresh(std::move(inputs), frontend, recording, outcome.compiled);
    save_outcome(*cache, outcome);
    compiled = std::move(outcome.compiled);
    return outcome.failed;
}

// Compile the batch, then link its executable or deliver its artifacts.
bool compile_and_link(std::vector<codegen::CompilationInput> inputs, const FrontendRequest &frontend,
                      const DiagnosticSink &sink) {
    const auto modules = inputs.size();
    std::optional<Compiled> compiled;
    bool cached = false;
    StageClock clock; // Added for --print-stage-times.
    const bool failed =
        clock.time([&] { return compile_or_reuse(std::move(inputs), frontend, sink, compiled, cached); });
    if (frontend.stage_times) {
        print_stage_time("compile", batch_text(modules, frontend), clock.elapsed(), cached);
    }
    if (failed || !compiled) {
        return failed;
    }
    if (frontend.executable_output) {
        link(*compiled, frontend, *frontend.executable_output, sink);
    } else {
        deliver(std::move(*compiled), frontend);
    }
    return false;
}
} // namespace

bool compile_batch(std::vector<codegen::CompilationInput> inputs, const FrontendRequest &request,
                   const DiagnosticSink &sink) {
    try {
        return compile_and_link(std::move(inputs), request, sink);
    } catch (const std::exception &error) {
        sink("error: " + std::string(error.what()));
    }
    return true;
}
} // namespace clause::cli
