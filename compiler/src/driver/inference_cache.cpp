// Added for the inference cache (plan step 58W).
#include "inference_cache.hpp"
#include "../project/paths.hpp"
#include "../semantic/types/inference_store.hpp"
#include <algorithm>
#include <iostream>

namespace clause::cli {
namespace {
// The stored value's format: one part "results" holding semantic::types::encode_module's bytes.
constexpr std::uint32_t RESULTS_FORMAT = 1;
constexpr std::string_view RESULTS = "results";

// What identifies a batch module's source for keys: path, fingerprint, predefined forms, escript and library
// status; empty for a module without a fingerprint, which cannot be keyed.
std::string identity(const codegen::CompilationInput &input) {
    if (input.fingerprint_.empty()) {
        return {};
    }
    return project::path_text(input.source_path) + "\n" + input.fingerprint_ + "\n" +
           std::to_string(input.predefined_) + (input.escript ? " escript" : "") + (input.library_ ? " library" : "");
}

// For each batch module, the other batch modules its atoms name: every module whose functions its facts can reach.
std::vector<std::vector<std::size_t>> named_modules(std::span<const std::unique_ptr<semantic::Module>> modules) {
    std::map<std::u32string, std::size_t> indices;
    for (std::size_t index = 0; index < modules.size(); ++index) {
        indices.emplace(modules[index]->name, index);
    }
    std::vector<std::vector<std::size_t>> result(modules.size());
    for (std::size_t index = 0; index < modules.size(); ++index) {
        for (const auto &atom : semantic::named_atoms(*modules[index]->syntax)) {
            const auto found = indices.find(atom);
            if (found != indices.end() && found->second != index) {
                result[index].push_back(found->second);
            }
        }
    }
    return result;
}

// The batch modules `start` reaches through `named`, itself excluded.
std::vector<std::size_t> reached(const std::vector<std::vector<std::size_t>> &named, const std::size_t start) {
    std::vector<bool> seen(named.size());
    seen[start] = true;
    std::vector<std::size_t> pending{start};
    std::vector<std::size_t> result;
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        for (const auto module : named[next]) {
            if (!seen[module]) {
                seen[module] = true;
                pending.push_back(module);
                result.push_back(module);
            }
        }
    }
    return result;
}

// The key of a covered module's results: clau's identity, the module's source identity and the sorted identities of
// the modules it reaches; none when one of them cannot be keyed.
std::optional<cache::Key> results_key(std::span<const codegen::CompilationInput> inputs, const std::size_t module,
                                      const std::vector<std::size_t> &others) {
    std::vector<std::string> identities;
    identities.reserve(others.size());
    for (const auto other : others) {
        identities.push_back(identity(inputs[other]));
    }
    const auto own = identity(inputs[module]);
    if (own.empty() || std::ranges::contains(identities, std::string())) {
        return std::nullopt;
    }
    std::ranges::sort(identities);
    cache::KeyBuilder key;
    key.add(CLAUSE_VERSION).add(cache::program_identity()).add("inference").add(own);
    for (const auto &text : identities) {
        key.add(text);
    }
    return key.finish();
}

// Where a module's results are kept: by its source's directory and file name, per project target.
cache::Address results_address(const std::filesystem::path &source, const std::string &target) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(source, error);
    const auto &path = error ? source : absolute;
    return {path.parent_path(), project::path_text(path.filename()),
            "inference@" + (target.empty() ? std::string("positional") : target)};
}
} // namespace

BatchInferenceCache::BatchInferenceCache(const InferenceCache &cache,
                                         const std::span<const codegen::CompilationInput> inputs,
                                         const std::span<const std::unique_ptr<semantic::Module>> modules)
    : cache_(cache), inputs_(inputs), modules_(modules) {
    std::vector<std::size_t> covered;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        if (cache.dirs_->covers(inputs[index].source_path)) {
            covered.push_back(index);
        }
    }
    if (covered.empty()) {
        return;
    }
    const auto named = named_modules(modules);
    for (const auto module : covered) {
        if (auto key = results_key(inputs, module, reached(named, module))) {
            entries_.push_back({module, results_address(inputs[module].source_path, cache.target_), std::move(*key)});
        }
    }
}

void BatchInferenceCache::load(semantic::types::Inference &inference) {
    for (auto &entry : entries_) {
        const auto found = cache_.store_->get(entry.address_, entry.key_, RESULTS_FORMAT);
        const auto *part = found.value_ ? cache::find_part(*found.value_, RESULTS) : nullptr;
        entry.reused_ = part && semantic::types::decode_module(inference, *modules_[entry.module_], part->bytes_);
        if (cache_.verbose_) {
            trace(entry, found);
        }
    }
}

void BatchInferenceCache::trace(const Entry &entry, const cache::Lookup &found) {
    if (entry.reused_) {
        std::cerr << "[cache] hit inference " << entry.address_.file_ << '\n';
        return;
    }
    // A kept value that did not decode lacks a function live now, or is damaged.
    const auto reason = found.value_ ? std::string_view("results do not fit") : cache::miss_text(found.miss_);
    std::cerr << "[cache] miss inference " << entry.address_.file_ << ": " << reason << '\n';
}

void BatchInferenceCache::save(const semantic::types::Inference &inference) const {
    for (const auto &entry : entries_) {
        if (!entry.reused_) {
            const auto bytes = semantic::types::encode_module(inference, *modules_[entry.module_]);
            cache_.store_->put(entry.address_, entry.key_, {RESULTS_FORMAT, {{std::string(RESULTS), bytes}}});
        }
    }
}

std::size_t BatchInferenceCache::reused() const {
    return static_cast<std::size_t>(std::ranges::count_if(entries_, [](const Entry &entry) { return entry.reused_; }));
}
} // namespace clause::cli
