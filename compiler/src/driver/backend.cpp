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
#include "compile_times.hpp"
#include "inspection.hpp"
#include "progress.hpp"
#include "publication.hpp"
#include "stage_times.hpp"
#include "type_report.hpp"
#include <algorithm>
#include <clause/abi/builtins.hpp>
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
    // Every hardware thread unless --jobs says otherwise; IR and assembly inspections read source scopes of the
    // batch's one context (parallel code generation).
    request.jobs = options.inspect_ir() ? 1 : options.jobs.value_or(0);
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
    // Added for the ssl runtime archive (plan 21 step LIB81B).
    result.ssl = compilation.request().startup && compilation.request().startup->ssl;
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
bool generate(codegen::Compilation &compilation, Analysis &analysis, const FrontendRequest &frontend) {
    {
        const auto lap = analysis.times.lap("lower");
        if (!codegen::lower(compilation, analysis.modules, *analysis.inferred) || !codegen::lower_frames(compilation)) {
            return false;
        }
    }
    if (frontend.backend.inspect_ir()) {
        return inspect_ir(compilation, frontend);
    }
    {
        const auto lap = analysis.times.lap("optimize");
        if (!codegen::optimize(compilation)) {
            return false;
        }
    }
    const auto lap = analysis.times.lap("emit");
    return emit(compilation);
}

// The crypto archive's link of a batch: with --with-crypto, or when the batch compiled the library's crypto module or
// calls the ssl engine, whose archive needs libcrypto; a source tree to link is built below the build directory's
// caches. Added for the crypto runtime archive.
std::optional<linking::CryptoLink> crypto_link(const Compiled &compiled, const FrontendRequest &frontend) {
    const auto library = (linking::library_directory() / "crypto" / "crypto.erl").lexically_normal();
    const bool compiled_crypto = std::ranges::any_of(compiled.inputs, [&library](const auto &input) {
        std::error_code error;
        return std::filesystem::equivalent(input, library, error);
    });
    if (!frontend.backend.links_crypto() && !compiled_crypto && !compiled.ssl) {
        return std::nullopt;
    }
    const auto cache = frontend.build_directory_ ? *frontend.build_directory_ / "cache"
                                                 : std::filesystem::temp_directory_path() / "clause-cache";
    return linking::CryptoLink{frontend.backend.openssl, cache, frontend.backend.fips, compiled.ssl};
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
             .lto = frontend.backend.lto,
             .epmd_executable = frontend.backend.epmd_executable,
             .bundled_epmd = !frontend.backend.no_bundled_epmd,
             .crypto = crypto_link(compiled, frontend)});
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

// Whether the program links the crypto runtime archive: --with-crypto, or the library crypto module in the batch.
// Added for the crypto runtime archive (plan 21 step LIB79).
bool uses_crypto(const Analysis &analysis, const FrontendRequest &frontend) {
    return frontend.backend.links_crypto() || std::ranges::any_of(analysis.modules, [](const auto &module) {
               return module->library_ && module->name == U"crypto";
           });
}

// Whether a live function calls a builtin of the ssl engine: calls of catalog builtins name no module, so its source
// never joins the batch. Added for the ssl runtime archive (plan 21 step LIB81B).
bool calls_ssl_engine(const Analysis &analysis) {
    const auto engine_call = [](const auto &service) {
        return service.second.builtin &&
               abi::v1::bridge_builtins.at(*service.second.builtin).module == "clause_ssl_engine";
    };
    return std::ranges::any_of(analysis.modules, [&engine_call](const auto &module) {
        return std::ranges::any_of(module->functions, [&engine_call](const semantic::Function &function) {
            return function.live_ && std::ranges::any_of(function.services, engine_call);
        });
    });
}

// Select the startup module from the resolved entry, if any.
std::optional<codegen::StartupRequest> startup_request(const Analysis &analysis, const FrontendRequest &frontend) {
    if (!analysis.entry) {
        return std::nullopt;
    }
    const auto &entry = *analysis.entry;
    return codegen::StartupRequest{entry.module,
                                   utf8(entry.function.name),
                                   entry.function.arity,
                                   entry.escript,
                                   uses_crypto(analysis, frontend),
                                   frontend.backend.fips.has_value(),
                                   calls_ssl_engine(analysis),
                                   !frontend.backend.no_bundled_epmd};
}

// The batch a compile time belongs to: its module count, and its project target when there is one.
// Added for --print-stage-times.
std::string batch_text(const std::size_t modules, const FrontendRequest &frontend) {
    auto text = std::to_string(modules) + (modules == 1 ? " module" : " modules");
    return frontend.project_target.empty() ? text : "target " + frontend.project_target + " (" + text + ")";
}

// Executables leave out what their entry cannot reach, and so do IR and assembly inspections given an entry, so they
// show the executable's code. Inspections with an entry added for inspection liveness.
bool live_only(const FrontendRequest &frontend) {
    const auto &backend = frontend.backend;
    return !backend.keep_exports && !backend.shell &&
           (frontend.executable_output || (backend.inspect_ir() && frontend.entry));
}

// The entry the startup code calls: the selected one, else the shell's with --shell. Added for the shell option
// (plan 21 step LIB74).
std::optional<project::SelectedEntry> batch_entry(const FrontendRequest &frontend) {
    if (frontend.entry || !frontend.backend.shell) {
        return frontend.entry;
    }
    return project::SelectedEntry{.name_ = {.module_ = U"clause_shell", .function_ = U"run", .arity_ = 1},
                                  .origin_ = "--shell"};
}

// The inference cache of a batch with a build cache; none without one or with --no-cache. Added for the inference
// cache (58W).
std::optional<InferenceCache> inference_cache(const FrontendRequest &frontend) {
    if (!frontend.caches_ || !frontend.build_directory_) {
        return std::nullopt;
    }
    return InferenceCache{&frontend.caches_->store(*frontend.build_directory_), &frontend.cache_dirs_,
                          frontend.project_target, frontend.verbose};
}

// Analyze before constructing LLVM state; moving the vector preserves borrowed AST addresses. A successful batch
// leaves its compilation in `output` unless it stopped at --print-types.
bool compile_analyzed(codegen::CompilationRequest request, const FrontendRequest &frontend, const DiagnosticSink &sink,
                      Analysis &analysis, std::optional<codegen::Compilation> &output) {
    const EntryRequest entry{batch_entry(frontend), frontend.executable_output.has_value(),
                             !request.project_target.empty()};
    const auto cache = inference_cache(frontend);
    if (!analyze(request, entry, live_only(frontend), analysis, sink, cache ? &*cache : nullptr)) {
        return true;
    }
    if (frontend.backend.print_types) {
        print_types(analysis, request);
        return false;
    }
    request.startup = startup_request(analysis, frontend);
    auto &compilation = output.emplace(std::move(request));
    const bool succeeded = generate(compilation, analysis, frontend);
    report_backend(compilation.result(), sink);
    return !succeeded || !compilation.result().complete();
}

// Compile the batch, printing the times of the phases it ran under --print-stage-times.
bool compile(std::vector<codegen::CompilationInput> inputs, const FrontendRequest &frontend, const DiagnosticSink &sink,
             std::optional<codegen::Compilation> &output) {
    const auto subject = batch_text(inputs.size(), frontend);
    Analysis analysis;
    analysis.times = PhaseTimes(frontend.stage_times);
    const bool failed =
        compile_analyzed(backend_request(std::move(inputs), frontend), frontend, sink, analysis, output);
    if (frontend.stage_times) {
        print_compile_phases(analysis, subject);
    }
    return failed;
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
        print_stage_time("compile", batch_text(modules, frontend), clock.elapsed(), cached ? "cached" : "");
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
