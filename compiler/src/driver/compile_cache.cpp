// Added for the compile cache (plan step 62D4).
#include "compile_cache.hpp"
#include "../codegen/target.hpp"
#include "../project/paths.hpp"
#include <algorithm>
#include <array>
#include <iostream>

namespace clause::cli {
namespace {
// The stored value's format: parts "failed", "diagnostic" (in order) and, when outputs are kept, "triple",
// "extension" and one part per output named by output_name.
constexpr std::uint32_t OUTCOME_FORMAT = 2;
// Output kinds as part names spell them, in OutputKind order.
constexpr std::array<std::string_view, 3> OUTPUT_KINDS{"object", "llvm_ir", "llvm_bitcode"};
constexpr std::string_view STARTUP = "+startup";

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
    flag(text, backend.shell, "shell");
    flag(text, backend.with_crypto, "with-crypto");
    flag(text, backend.fips.has_value(), "fips"); // Added for crypto's FIPS mode.
    flag(text, frontend.executable_output.has_value(), "executable");
    flag(text, outputs, "outputs");
    if (frontend.entry) {
        text += "\nentry " + project::entry_spelling(frontend.entry->name_) + " " + frontend.entry->origin_;
    }
    return text + "\ntarget " + frontend.project_target;
}

// Where a batch's outcome is kept: in the build directory, by target (or "positional") and options, so builds of
// one target with other options keep their own entries.
cache::Address outcome_address(const FrontendRequest &frontend, const std::string &options) {
    const auto name = frontend.project_target.empty() ? std::string("positional") : frontend.project_target;
    return {*frontend.build_directory_, name + "@" + cache::hash_text(options).substr(0, 8), "compile"};
}

// An output's part name: its kind, "+startup" for the startup object, then ':' and the module name.
std::string output_name(const codegen::OutputBuffer &output) {
    return std::string(OUTPUT_KINDS.at(static_cast<std::size_t>(output.kind))) +
           (output.startup ? std::string(STARTUP) : "") + ":" + output.module_name;
}

// The output a part holds; none for a name output_name does not make.
std::optional<codegen::OutputBuffer> read_output(const cache::Part &part) {
    const auto colon = part.name_.find(':');
    std::string_view head = std::string_view(part.name_).substr(0, colon);
    const bool startup = head.ends_with(STARTUP);
    head.remove_suffix(startup ? STARTUP.size() : 0);
    const auto kind = std::ranges::find(OUTPUT_KINDS, head);
    if (colon == std::string::npos || kind == OUTPUT_KINDS.end()) {
        return std::nullopt;
    }
    codegen::OutputBuffer output{
        part.name_.substr(colon + 1), static_cast<codegen::OutputKind>(kind - OUTPUT_KINDS.begin()), {}, startup};
    const auto *data = reinterpret_cast<const std::byte *>(part.bytes_.data());
    output.bytes.assign(data, data + part.bytes_.size());
    return output;
}

// A stored outcome while its parts are read back.
struct Reading {
    CompileOutcome outcome;
    std::optional<std::string> triple;
    std::optional<std::string> extension;
    codegen::CompilationResult result;
};

// Apply one part to the outcome being read; false when the part is damaged.
bool read_part(const cache::Part &part, Reading &reading) {
    if (part.name_ == "failed") {
        reading.outcome.failed = part.bytes_ == "1";
    } else if (part.name_ == "diagnostic") {
        reading.outcome.diagnostics.push_back(part.bytes_);
    } else if (part.name_ == "triple") {
        reading.triple = part.bytes_;
    } else if (part.name_ == "extension") {
        reading.extension = part.bytes_;
    } else {
        auto output = read_output(part);
        return output && reading.result.add_output(std::move(*output));
    }
    return true;
}

// A stored outcome; none when damaged. Outputs are kept when the outcome names its triple and extension.
std::optional<CompileOutcome> decode(const cache::Value &value, std::span<const std::filesystem::path> inputs) {
    Reading reading;
    for (const auto &part : value.parts_) {
        if (!read_part(part, reading)) {
            return std::nullopt;
        }
    }
    if (reading.triple && reading.extension) {
        reading.result.complete();
        reading.outcome.compiled =
            Compiled{std::move(reading.result), std::move(*reading.triple), std::move(*reading.extension),
                     std::vector<std::filesystem::path>(inputs.begin(), inputs.end())};
    }
    return std::move(reading.outcome);
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
    const auto found = cache.store_->get(cache.address_, cache.key_, OUTCOME_FORMAT);
    auto outcome = found.value_ ? decode(*found.value_, inputs) : std::nullopt;
    if (cache.verbose_) {
        const auto miss = found.value_ ? cache::Miss::corrupt : found.miss_;
        std::cerr << "[cache] " << (outcome ? "hit" : "miss") << " compile " << cache.address_.file_
                  << (outcome ? "" : ": " + std::string(cache::miss_text(miss))) << '\n';
    }
    return outcome;
}

void save_outcome(const CompileCache &cache, const CompileOutcome &outcome) {
    cache::Value value{OUTCOME_FORMAT, {{"failed", outcome.failed ? "1" : "0"}}};
    for (const auto &text : outcome.diagnostics) {
        value.parts_.push_back({"diagnostic", text});
    }
    if (cache.outputs_ && outcome.compiled) {
        value.parts_.push_back({"triple", outcome.compiled->triple});
        value.parts_.push_back({"extension", outcome.compiled->object_extension});
        for (const auto &output : outcome.compiled->result.outputs()) {
            const auto *data = reinterpret_cast<const char *>(output.bytes.data());
            value.parts_.push_back({output_name(output), std::string(data, output.bytes.size())});
        }
    }
    cache.store_->put(cache.address_, cache.key_, value);
}
} // namespace clause::cli
