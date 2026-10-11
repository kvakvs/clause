// Added for the compile cache (plan step 62D4).
#include "compile_cache.hpp"
#include "../codegen/target.hpp"
#include "../project/paths.hpp"
#include <iostream>

namespace clause::cli {
namespace {
// The stored value's format; another format reads as a damaged entry.
constexpr std::string_view FORMAT = "clause-compile-outcome-1";

// Append an option to a key text when it is set.
void flag(std::string &text, const bool set, const std::string_view name) {
    if (set) {
        text += '\n';
        text += name;
    }
}

// The options a batch's compilation depends on, as one key part.
std::string options_text(const FrontendRequest &frontend, const bool outputs) {
    const auto &backend = frontend.backend;
    auto text = codegen::resolve_triple(backend.target_triple);
    text += "\nO" + std::to_string(static_cast<int>(backend.optimization.value_or(codegen::OptimizationLevel::none)));
    text += "\nemit " + std::to_string(backend.emit ? static_cast<int>(*backend.emit) : -1);
    flag(text, backend.disable_type_specialization, "no-type-specialization");
    flag(text, backend.debug_info, "debug-info");
    flag(text, backend.lto, "lto");
    flag(text, backend.keep_exports, "keep-exports");
    flag(text, frontend.executable_output.has_value(), "executable");
    flag(text, outputs, "outputs");
    if (frontend.entry) {
        text += "\nentry " + project::entry_text(frontend.entry->name) + " " + frontend.entry->origin;
    }
    return text + "\ntarget " + frontend.project_target;
}

// Where a batch's outcome is kept: in the build directory, by target (or "positional") and options, so builds of
// one target with other options keep their own entries.
cache::Address outcome_address(const FrontendRequest &frontend, const std::string &options) {
    const auto name = frontend.project_target.empty() ? std::string("positional") : frontend.project_target;
    return {*frontend.build_directory_, name + "@" + cache::hash_text(options).substr(0, 8), "compile"};
}

void put_outputs(std::string &out, const codegen::CompilationResult &result) {
    cache::put_number(out, result.outputs().size());
    for (const auto &output : result.outputs()) {
        cache::put_text(out, output.module_name);
        cache::put_number(out, static_cast<std::uint64_t>(output.kind));
        cache::put_number(out, output.startup ? 1 : 0);
        cache::put_text(out, {reinterpret_cast<const char *>(output.bytes.data()), output.bytes.size()});
    }
}

std::optional<codegen::OutputBuffer> read_output(cache::Reader &reader) {
    auto name = reader.text();
    const auto kind = reader.number();
    const auto startup = reader.number();
    const auto bytes = reader.text();
    if (!name || !kind || *kind > static_cast<std::uint64_t>(codegen::OutputKind::llvm_bitcode) || !startup || !bytes) {
        return std::nullopt;
    }
    codegen::OutputBuffer output{std::move(*name), static_cast<codegen::OutputKind>(*kind), {}, *startup != 0};
    const auto *data = reinterpret_cast<const std::byte *>(bytes->data());
    output.bytes.assign(data, data + bytes->size());
    return output;
}

// The outputs of a stored outcome as a completed result; none when damaged.
std::optional<codegen::CompilationResult> read_outputs(cache::Reader &reader) {
    const auto count = reader.number();
    if (!count) {
        return std::nullopt;
    }
    codegen::CompilationResult result;
    for (std::uint64_t i = 0; i < *count; ++i) {
        auto output = read_output(reader);
        if (!output || !result.add_output(std::move(*output))) {
            return std::nullopt;
        }
    }
    result.complete();
    return result;
}

// The diagnostics of a stored outcome; none when damaged.
std::optional<std::vector<std::string>> read_diagnostics(cache::Reader &reader) {
    const auto count = reader.number();
    if (!count) {
        return std::nullopt;
    }
    std::vector<std::string> result;
    for (std::uint64_t i = 0; i < *count; ++i) {
        auto text = reader.text();
        if (!text) {
            return std::nullopt;
        }
        result.push_back(std::move(*text));
    }
    return result;
}

// The kept outputs of a stored outcome, if it has them; false when damaged.
bool read_compiled(cache::Reader &reader, std::span<const std::filesystem::path> inputs,
                   std::optional<Compiled> &compiled) {
    const auto kept = reader.number();
    if (!kept || *kept == 0) {
        return kept.has_value();
    }
    auto triple = reader.text();
    auto extension = reader.text();
    auto result = triple && extension ? read_outputs(reader) : std::nullopt;
    if (!result) {
        return false;
    }
    compiled = Compiled{std::move(*result), std::move(*triple), std::move(*extension),
                        std::vector<std::filesystem::path>(inputs.begin(), inputs.end())};
    return true;
}

// A stored outcome; none when damaged or of another format.
std::optional<CompileOutcome> decode(std::string_view bytes, std::span<const std::filesystem::path> inputs) {
    cache::Reader reader(bytes);
    const auto format = reader.text();
    const auto failed = reader.number();
    auto diagnostics = format == FORMAT && failed ? read_diagnostics(reader) : std::nullopt;
    if (!diagnostics) {
        return std::nullopt;
    }
    CompileOutcome outcome{*failed != 0, std::move(*diagnostics), {}};
    if (!read_compiled(reader, inputs, outcome.compiled) || !reader.empty()) {
        return std::nullopt;
    }
    return outcome;
}
} // namespace

std::optional<CompileCache> compile_cache(std::span<const codegen::CompilationInput> inputs,
                                          const FrontendRequest &frontend) {
    const auto &backend = frontend.backend;
    if (!frontend.caches_ || !frontend.build_directory_ || backend.print_types || backend.inspect_ir()) {
        return std::nullopt;
    }
    const bool outputs = frontend.executable_output.has_value() || backend.emit.has_value();
    const auto options = options_text(frontend, outputs);
    cache::KeyBuilder key;
    key.add(CLAUSE_VERSION).add(cache::program_identity()).add(options);
    for (const auto &input : inputs) {
        if (input.fingerprint_.empty()) {
            return std::nullopt;
        }
        key.add(project::path_text(input.source_path)).add(input.fingerprint_);
        key.add(std::to_string(input.predefined_) + (input.escript ? " escript" : ""));
    }
    return CompileCache{&frontend.caches_->store(*frontend.build_directory_), outcome_address(frontend, options),
                        key.finish(), outputs, frontend.verbose};
}

std::optional<CompileOutcome> load_outcome(const CompileCache &cache, std::span<const std::filesystem::path> inputs) {
    auto found = cache.store_->get(cache.address_, cache.key_);
    auto outcome = found.bytes_ ? decode(*found.bytes_, inputs) : std::nullopt;
    if (cache.verbose_) {
        const auto miss = found.bytes_ ? cache::Miss::corrupt : found.miss_;
        std::cerr << "[cache] " << (outcome ? "hit" : "miss") << " compile " << cache.address_.file_
                  << (outcome ? "" : ": " + std::string(cache::miss_text(miss))) << '\n';
    }
    return outcome;
}

void save_outcome(const CompileCache &cache, const CompileOutcome &outcome) {
    std::string out;
    cache::put_text(out, FORMAT);
    cache::put_number(out, outcome.failed ? 1 : 0);
    cache::put_number(out, outcome.diagnostics.size());
    for (const auto &text : outcome.diagnostics) {
        cache::put_text(out, text);
    }
    const bool compiled = cache.outputs_ && outcome.compiled;
    cache::put_number(out, compiled ? 1 : 0);
    if (compiled) {
        cache::put_text(out, outcome.compiled->triple);
        cache::put_text(out, outcome.compiled->object_extension);
        put_outputs(out, outcome.compiled->result);
    }
    cache.store_->put(cache.address_, cache.key_, out);
}
} // namespace clause::cli
