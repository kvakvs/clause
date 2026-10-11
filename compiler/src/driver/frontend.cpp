#include "frontend.hpp"
#include "../codegen/limits.hpp"
#include "../codegen/request.hpp"
#include "../linking/link.hpp"
#include "../project/paths.hpp"
#include "../semantic/calls.hpp"
#include "backend.hpp"
#include "escript.hpp"
#include "options.hpp"
#include "placeholders.hpp"
#include "predefined.hpp"
#include "stage_times.hpp"
#include "transform_sources.hpp"
#include "transforms/abstract_input.hpp"
#include "transforms/export.hpp"
#include <algorithm>
#include <clause/compiler/parser.hpp>
#include <clause/compiler/printing.hpp>
#include <clause/compiler/source.hpp>
#include <fstream>
#include <iostream>

namespace clause::cli {
namespace {
// Retain native filename text in encoding, I/O, and ingestion messages.
std::string filename(const std::filesystem::path &path) {
    const auto bytes = path.generic_u8string();
    return {bytes.begin(), bytes.end()};
}

// Keep ingestion messages out of source/AST output and project diagnostic wrappers.
void trace_ingestion(const bool verbose, const std::string_view stage, const std::filesystem::path &path) {
    if (verbose) {
        std::cerr << '[' << stage << "] " << filename(path) << '\n';
    }
}

// Trace resolved includes while preserving any caller-provided observation callback.
PreprocessorOptions preprocessing_options(const FrontendRequest &request) {
    auto options = request.preprocessing;
    if (request.verbose) {
        options.include_loaded = [previous = std::move(options.include_loaded)](const auto &path) {
            trace_ingestion(true, "pp", path);
            if (previous) {
                previous(path);
            }
        };
    }
    return options;
}

// Preserve severity and existing logical/physical source rendering in every caller.
void print_diagnostic(const Diagnostic &diagnostic, const DiagnosticSink &sink) {
    const auto prefix = diagnostic.severity == Severity::warning ? "warning: " : "error: ";
    sink(prefix + clause::render(diagnostic));
}

// Emit expanded source without adding project-specific stdout banners.
void print_form(const PreprocessorEvent &event) {
    if (const auto *form = std::get_if<OrdinaryForm>(&event)) {
        print_preprocessed(std::cout, *form);
    }
}

// Own syntax for the entire batch before borrowing it in semantic side tables.
using Inputs = std::vector<codegen::CompilationInput>;

// Time one file spends being read and preprocessed, and being parsed; the parser pulls forms from the preprocessor,
// so the two stages interleave. Added for --print-stage-times.
struct FileTimes {
    StageClock preprocess;
    StageClock parse;
    // Parse transforms of the module (host erl run and import of its forms); set only when transforms ran.
    std::optional<StageClock> transform;
};

// What a parsing pass leaves besides the syntax: the module's features, whether its input failed, and where its
// main source ends for the abstract format's eof form.
struct ParsedModule {
    FeatureSnapshot features;
    bool failed;
    std::function<Position(const ast::Module &)> end;
};

// Print a parsed module as the request asks: as a syntax tree, as source, as abstract forms.
void print_parsed(const FrontendRequest &request, const ast::Module &module, const ParsedModule &parsed) {
    if (request.print_ast) {
        print_ast(std::cout, module);
    }
    if (request.print_source) {
        print_source(std::cout, module);
    }
    if (request.print_abstr) {
        // Added for parse transforms.
        std::cout << transforms::abstract_text(transforms::export_module(module, module.forms(), parsed.end(module)));
    }
}

// Feed every preprocessed event to the parser until EOF or a parser resource limit.
void parse_events(PreprocessorSession &session, const FrontendRequest &request, ParserSession &parser,
                  FileTimes &times) {
    while (!parser.stopped()) {
        const auto event = times.preprocess.time([&] { return session.next(); });
        if (!event) {
            return;
        }
        if (request.print_pp) {
            print_form(*event);
        }
        times.parse.time([&] { parser.consume(*event); });
    }
}

// Finish a parsing pass and dispatch a successful module to the requested final stage; compiled modules get OTP's
// predefined functions.
bool finish_module(ParserSession &parser, const ParsedModule &parsed, const FrontendRequest &request,
                   const DiagnosticSink &sink, const std::filesystem::path &path, const bool escript, Inputs &inputs,
                   FileTimes &times) {
    const auto predefined =
        request.compile && !parser.stopped() ? add_predefined(parser, parsed.features, path, escript) : std::size_t{0};
    auto result = times.parse.time([&] { return std::move(parser).finish(parsed.features); });
    for (const auto &diagnostic : result.diagnostics) {
        print_diagnostic(diagnostic, sink);
    }
    print_parsed(request, result.module, parsed);
    if (result.failed || parsed.failed) {
        return true;
    }
    if (request.compile) {
        codegen::validate_input_limits(inputs, {}, &result.module);
        inputs.emplace_back(path, std::move(result.module));
        inputs.back().escript = escript;
        inputs.back().predefined_ = predefined;
    }
    return false;
}

// Whether the request's action sees modules after their parse transforms (docs/transforms.md#clause-pipeline).
// Added for parse transforms.
bool transforming(const FrontendRequest &request) {
    return request.compile || request.print_ast || request.print_source || request.print_abstr;
}

// The compile options a transform receives for this request. Added for parse transforms.
transforms::CompileOptions compile_options(const FrontendRequest &request, const FeatureSnapshot &features) {
    return {.features_ = features ? features->enabled : std::vector<std::string>{},
            .includes_ = request.preprocessing.include_paths,
            .defines_ = request.preprocessing.definitions,
            .transforms_ = request.transforms_.transforms_};
}

// Parse transforms run on a module that parsed without errors; the forms they return replace its syntax.
// Added for parse transforms.
bool transform_and_finish(ParserSession &parser, PreprocessorSession &session, const FrontendRequest &request,
                          const DiagnosticSink &sink, const SourcePtr &source, const std::filesystem::path &path,
                          const bool escript, Inputs &inputs, FileTimes &times) {
    const auto features = session.features();
    auto parsed = times.parse.time([&] { return std::move(parser).finish(features); });
    for (const auto &diagnostic : parsed.diagnostics) {
        print_diagnostic(diagnostic, sink);
    }
    if (parsed.failed || session.failed()) {
        return true;
    }
    ParserSession transformed;
    const auto outcome = times.transform.emplace().time([&] {
        return transforms::transform_module(parsed.module, *source, request.transforms_,
                                            compile_options(request, features), features, transformed);
    });
    if (!outcome.output_.empty()) {
        std::cerr << outcome.output_;
    }
    for (const auto &diagnostic : outcome.diagnostics_) {
        print_diagnostic(diagnostic, sink);
    }
    if (outcome.failed_) {
        return true;
    }
    const auto end = [position = outcome.end_](const ast::Module &) { return position; };
    return finish_module(transformed, {features, false, end}, request, sink, path, escript, inputs, times);
}

// Consume a preprocessing session as one module.
bool parse_and_print(PreprocessorSession &session, const FrontendRequest &request, const DiagnosticSink &sink,
                     const SourcePtr &source, const std::filesystem::path &path, const bool escript, Inputs &inputs,
                     FileTimes &times) {
    ParserSession parser;
    parse_events(session, request, parser, times);
    if (transforming(request) && !parser.stopped() &&
        transforms::wants_transforms(parser.view(), request.transforms_)) {
        return transform_and_finish(parser, session, request, sink, source, path, escript, inputs, times);
    }
    const auto end = [&source](const ast::Module &module) {
        return transforms::end_of(module, module.forms(), *source);
    };
    const ParsedModule parsed{session.features(), session.failed(), end};
    return finish_module(parser, parsed, request, sink, path, escript, inputs, times);
}

// The frontend stages one file went through. Added for --print-stage-times.
struct FileStages {
    bool preprocessed;
    bool parsed;
};

// Print the times of the stages a file went through. Added for --print-stage-times.
void print_file_times(const FrontendRequest &request, const std::filesystem::path &path, const FileTimes &times,
                      const FileStages stages) {
    if (!request.stage_times) {
        return;
    }
    if (stages.preprocessed) {
        print_stage_time("preprocess", filename(path), times.preprocess.elapsed());
    }
    if (stages.parsed) {
        print_stage_time("parse", filename(path), times.parse.elapsed());
    }
    if (times.transform) {
        print_stage_time("transform", filename(path), times.transform->elapsed());
    }
}

// A file of abstract format forms compiles like `erlc File.abstr`. Added for parse transforms.
bool process_abstract(const std::filesystem::path &path, const FrontendRequest &request, const DiagnosticSink &sink,
                      Inputs &inputs) {
    trace_ingestion(request.verbose, "parse", path);
    ParserSession parser;
    FileTimes times;
    const auto file =
        times.parse.time([&] { return transforms::parse_abstract_file(path, preprocessing_options(request), parser); });
    bool failed = false;
    for (const auto &diagnostic : file.diagnostics_) {
        print_diagnostic(diagnostic, sink);
        failed = failed || diagnostic.severity == Severity::error;
    }
    const auto end = [position = file.end_](const ast::Module &) { return position; };
    failed = finish_module(parser, {file.features_, failed, end}, request, sink, path, false, inputs, times);
    print_file_times(request, path, times, {.preprocessed = false, .parsed = true});
    return failed;
}

// Read a source, rewriting a "#!" escript header and warning about ignored emulator arguments.
SourcePtr read_source(SourceManager &sources, const std::filesystem::path &path, const DiagnosticSink &sink,
                      bool &escript) {
    auto source = sources.read(path);
    const auto rewritten = escript_source(path, source->bytes);
    escript = rewritten.has_value();
    if (!rewritten) {
        return source;
    }
    if (rewritten->emulator_arguments) {
        sink("warning: " + filename(path) + ":" + std::to_string(*rewritten->emulator_arguments) +
             ":1: escript emulator arguments (%%!) are ignored by compiled executables");
    }
    return sources.add(source->name, rewritten->bytes);
}

// Whether the request parses its sources rather than only preprocessing them.
bool parsing(const FrontendRequest &request) {
    return request.parse_check || request.print_ast || request.print_source || request.print_abstr || request.compile;
}

// Report preprocessing diagnostics and print expanded forms without parsing.
bool preprocess_only(PreprocessorSession &session, const FrontendRequest &request, const DiagnosticSink &sink,
                     FileTimes &times) {
    while (const auto event = times.preprocess.time([&] { return session.next(); })) {
        if (const auto *diagnostic = std::get_if<Diagnostic>(&*event)) {
            print_diagnostic(*diagnostic, sink);
        }
        if (request.print_pp) {
            print_form(*event);
        }
    }
    return session.failed();
}

// Read, preprocess and, when requested, parse one source file.
bool process_source(const std::filesystem::path &path, const FrontendRequest &request, const DiagnosticSink &sink,
                    Inputs &inputs, FileTimes &times) {
    SourceManager sources;
    bool escript = false;
    const auto source = times.preprocess.time([&] { return read_source(sources, path, sink, escript); });
    trace_ingestion(request.verbose, "pp", path);
    auto session = times.preprocess.time([&] { return PreprocessorSession(source, preprocessing_options(request)); });
    if (!parsing(request)) {
        return preprocess_only(session, request, sink, times);
    }
    trace_ingestion(request.verbose, "parse", path);
    return parse_and_print(session, request, sink, source, path, escript, inputs, times);
}

// Keep source ownership and all mutable frontend state local to one file.
bool process_module(const std::filesystem::path &path, const FrontendRequest &request, const DiagnosticSink &sink,
                    Inputs &inputs) {
    if (path.extension() == ".abstr") {
        return process_abstract(path, request, sink, inputs);
    }
    FileTimes times;
    const bool failed = process_source(path, request, sink, inputs, times);
    print_file_times(request, path, times, {.preprocessed = true, .parsed = parsing(request)});
    return failed;
}

// Preserve per-file recovery while retaining successful syntax for the batch.
bool process_file(const std::filesystem::path &path, const FrontendRequest &request, const DiagnosticSink &diagnostics,
                  Inputs &inputs) {
    try {
        return process_module(path, request, diagnostics, inputs);
    } catch (const EncodingError &error) {
        diagnostics(filename(path) + ": byte " + std::to_string(error.byte) + ": " + error.what());
    } catch (const std::exception &error) {
        diagnostics("error: " + filename(path) + ": " + error.what());
    }
    return true;
}

// Whether a module of the batch declares `name`.
bool defined(const Inputs &inputs, const std::u32string &name) {
    return std::ranges::any_of(inputs,
                               [&](const auto &input) { return semantic::declared_module(input.syntax) == name; });
}

// The first Module.erl in the directories, in order, passing over ignored ones (then `ignored` is set); empty when
// none exists.
std::filesystem::path module_source(const std::vector<std::filesystem::path> &directories, const std::u32string &name,
                                    const project::Ignore &ignore, bool &ignored) {
    const auto file = project::native_path(utf8(name) + ".erl");
    for (const auto &directory : directories) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(directory / file, error)) {
            continue;
        }
        if (!ignore.skips(directory / file)) {
            return directory / file;
        }
        ignored = true;
    }
    return {};
}

// Added for --print-inputs.
struct ReferencedInput {
    // A module source added because a batch module names it, and "file:line" of the first naming site.
    std::filesystem::path path;
    std::string site;
};

// Module discovery state shared by add_referenced and the listing. Added for --print-inputs.
struct Discovery {
    // Search directories in order (source_search_paths, then the library), and the module names already handled.
    std::vector<std::filesystem::path> directories;
    std::set<std::u32string> added;
    // Sources added by reference, in the order they joined the batch.
    std::vector<ReferencedInput> referenced;
    // Ignored modules the batch names, each with its first naming site, in discovery order. Added for ignored modules.
    std::vector<std::pair<std::u32string, std::string>> ignored;
};

// Render where a module was named as "file:line" of the token's logical location. Added for --print-inputs.
std::string site_text(const ast::Module &syntax, const ast::NodeSource &source) {
    const auto &location = syntax.anchor(source).location;
    return project::path_text(project::native_path(location.file)) + ":" + std::to_string(location.line);
}

// Load the module `name` when the batch lacks it and a search directory has it; true on a source failure.
// Added for --print-inputs.
bool add_module(const std::u32string &name, std::string site, Discovery &discovery, const FrontendRequest &request,
                const DiagnosticSink &sink, Inputs &inputs) {
    if (defined(inputs, name) || !discovery.added.insert(name).second) {
        return false;
    }
    // ignore_modules wins over search; a module found only in ignored places is ignored. Added for ignored modules.
    const auto &ignore = request.ignore_;
    bool passed = ignore.names(utf8(name));
    auto path = passed ? std::filesystem::path{} : module_source(discovery.directories, name, ignore, passed);
    if (path.empty()) {
        if (passed || ignore.skipped(utf8(name))) {
            discovery.ignored.emplace_back(name, std::move(site));
        }
        return false;
    }
    discovery.referenced.push_back({path, std::move(site)});
    return process_file(path, request, sink, inputs);
}

// Add the modules the batch references but does not define, from the search directories, then the library,
// including those they reference in turn.
bool add_referenced(const FrontendRequest &request, const DiagnosticSink &sink, Inputs &inputs, Discovery &discovery) {
    discovery.directories = request.module_search_paths_;
    discovery.directories.push_back(linking::library_directory());
    bool failed = false;
    for (std::size_t scanned = 0; scanned < inputs.size(); ++scanned) {
        for (const auto &[name, source] : semantic::referenced_modules(inputs[scanned].syntax)) {
            auto site = site_text(inputs[scanned].syntax, source);
            failed = add_module(name, std::move(site), discovery, request, sink, inputs) || failed;
        }
    }
    return failed;
}

// Compile the placeholder of one ignored module: the functions the batch names in it. Added for ignored modules.
bool add_placeholder(const std::u32string &module, const std::set<semantic::FunctionKey> &functions,
                     const DiagnosticSink &sink, Inputs &inputs) {
    FrontendRequest request;
    request.compile = true;
    SourceManager sources;
    const auto path = placeholder_path(module);
    const auto source = sources.add(project::path_text(path), placeholder_source(module, functions));
    PreprocessorSession session(source, request.preprocessing);
    FileTimes untimed; // Placeholders are generated, not files; their stage times are not printed.
    return parse_and_print(session, request, sink, source, path, false, inputs, untimed);
}

// Add a placeholder module for each ignored module the batch names. Added for ignored modules.
bool add_placeholders(const Discovery &discovery, const DiagnosticSink &sink, Inputs &inputs) {
    if (discovery.ignored.empty()) {
        return false;
    }
    std::map<std::u32string, std::set<semantic::FunctionKey>> functions;
    for (const auto &input : inputs) {
        for (auto &[module, keys] : semantic::referenced_functions(input.syntax)) {
            functions[module].merge(keys);
        }
    }
    bool failed = false;
    for (const auto &[module, site] : discovery.ignored) {
        failed = add_placeholder(module, functions[module], sink, inputs) || failed;
    }
    return failed;
}

// List paths one per line as given. Added for --print-inputs.
void print_inputs(const std::span<const std::filesystem::path> paths) {
    for (const auto &path : paths) {
        std::cout << filename(path) << '\n';
    }
}

// Print the batch's inputs: listed sources, then each source added by reference with the site that named it.
// Added for --print-inputs.
void list_batch(const std::span<const std::filesystem::path> paths, const Discovery &discovery,
                const FrontendRequest &request) {
    if (request.multiple_targets) {
        std::cout << "[target " << request.project_target << "]\n";
    }
    print_inputs(paths);
    for (const auto &input : discovery.referenced) {
        std::cout << filename(input.path) << " (referenced at " << input.site << ")\n";
    }
    // Added for ignored modules.
    for (const auto &[module, site] : discovery.ignored) {
        std::cout << "ignored module " << utf8(module) << " (referenced at " << site << ")\n";
    }
}

// Compile the batch together with the placeholders of the ignored modules it names. Added for ignored modules.
bool compile_with_placeholders(Inputs inputs, const Discovery &discovery, const FrontendRequest &request,
                               const DiagnosticSink &sink) {
    if (add_placeholders(discovery, sink, inputs)) {
        return true;
    }
    return compile_batch(std::move(inputs), request, sink);
}
} // namespace

bool process_files(const std::span<const std::filesystem::path> paths, const FrontendRequest &shared,
                   const DiagnosticSink &sink) {
    // Project transform modules come from the batch and its search directories. Added for parse transforms.
    auto request = shared;
    // The batch keeps what it found in memory. Added for transform source caching.
    auto cache = std::make_shared<TransformSourceCache>();
    request.transforms_.find_sources_ = [paths, &shared, cache](const std::u32string &module,
                                                                const transforms::HostModule &host) {
        const SourcePlaces places{
            .inputs_ = paths, .directories_ = shared.module_search_paths_, .ignore_ = shared.ignore_};
        return transform_sources(module, places, shared.preprocessing, host, *cache);
    };
    Inputs inputs;
    if (request.compile && paths.size() > codegen::CompilationLimits{}.modules) {
        sink("error: compilation module count limit exceeded");
        return true;
    }
    bool failed = false;
    for (const auto &path : paths) {
        failed = process_file(path, request, sink, inputs) || failed;
    }
    Discovery discovery;
    if (request.compile && !failed) {
        failed = add_referenced(request, sink, inputs, discovery);
    }
    // Stop before compilation and list the batch. Added for --print-inputs.
    if (request.list_inputs) {
        list_batch(paths, discovery, request);
        return failed;
    }
    if (request.compile && !failed) {
        failed = compile_with_placeholders(std::move(inputs), discovery, request, sink);
    }
    return failed;
}

// Preserve positional order and warning-only success using the same per-file operation.
int process_inputs(const Options &options) {
    FrontendRequest request{
        options.print_pp, options.print_ast,     options.parse_check, !options.preprocess || options.print_inputs,
        options.verbose,  options.preprocessing, options.backend};
    request.list_inputs = options.print_inputs;      // Added for --print-inputs.
    request.stage_times = options.print_stage_times; // Added for --print-stage-times.
    request.print_source = options.print_source;
    request.print_abstr = options.print_abstr;
    request.transforms_ = options.transforms;
    if (options.output_explicit) {
        request.executable_output = options.output;
    }
    if (options.entry) {
        request.entry = project::SelectedEntry{*options.entry, "--entry"};
    }
    const DiagnosticSink sink = [](const std::string_view message) { std::cerr << message << '\n'; };
    return process_files(options.inputs, request, sink) ? 1 : 0;
}

// Check physical inputs before entering either preprocessing or future compilation.
bool validate_inputs(const std::vector<std::filesystem::path> &inputs) {
    for (const auto &input : inputs) {
        std::error_code error;
        const bool regular = std::filesystem::is_regular_file(input, error);
        if (error) {
            std::cerr << "clau: error: cannot access " << input << ": " << error.message() << '\n';
            return false;
        }
        if (!regular) {
            std::cerr << "clau: error: input is not a regular file: " << input << '\n';
            return false;
        }
        if (!std::ifstream{input, std::ios::binary}) {
            std::cerr << "clau: error: cannot read input: " << input << '\n';
            return false;
        }
    }
    return true;
}

} // namespace clause::cli
