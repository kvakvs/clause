// Added for build caches (plan step 62D2).
#include "build_cache.hpp"
#include "../project/paths.hpp"
#include <algorithm>
#include <iostream>

namespace clause::cli {
namespace {
namespace fs = std::filesystem;

// The format of a stored host run: its "reply" and "output" parts.
constexpr std::uint32_t RUN_FORMAT = 1;

// Where a query's result is kept: beside the module it transforms, or beside the erl it asks.
cache::Address address(const transforms::HostQuery &query) {
    const auto file = query.kind_ == "sticky" ? query.request_.erl_ : project::native_path(query.request_.file_);
    std::error_code error;
    return {fs::absolute(file, error).parent_path(), project::path_text(file.filename()), std::string(query.kind_)};
}

// Add the files a transform source includes, as its discovery preprocessed it.
void add_includes(cache::KeyBuilder &key, const TransformSourceCache *sources, const fs::path &source) {
    if (!sources) {
        return;
    }
    const auto found = sources->includes_.find(source);
    if (found == sources->includes_.end()) {
        return;
    }
    for (const auto &include : found->second) {
        key.add_file(include);
    }
}

// The host run a lookup found, when it has both parts.
std::optional<transforms::HostRun> run_of(const cache::Lookup &found) {
    const auto *reply = found.value_ ? cache::find_part(*found.value_, "reply") : nullptr;
    const auto *output = found.value_ ? cache::find_part(*found.value_, "output") : nullptr;
    if (!reply || !output) {
        return std::nullopt;
    }
    return transforms::HostRun{reply->bytes_, output->bytes_};
}

// The entries of a directory with the extension (none: every entry without one), sorted.
std::vector<fs::path> files_with(const fs::path &directory, const std::string_view extension) {
    std::vector<fs::path> result;
    std::error_code error;
    for (const auto &item : fs::directory_iterator(directory, error)) {
        if (item.path().extension() == fs::path(extension)) {
            result.push_back(item.path());
        }
    }
    std::ranges::sort(result);
    return result;
}
} // namespace

cache::Store &BuildCaches::store(const fs::path &build_directory) {
    auto &store = stores_[build_directory];
    if (!store) {
        cache::StoreOptions options;
        options.notes_ = verbose_ ? &std::cerr : nullptr;
        store = cache::open_store(build_directory / "cache", options);
    }
    return *store;
}

void BuildCaches::prune() {
    for (const auto &[directory, store] : stores_) {
        store->prune();
    }
}

std::optional<transforms::HostRun> TransformCache::find(const transforms::HostQuery &query) {
    const auto where = address(query);
    const auto found = store_.get(where, key(query), RUN_FORMAT);
    auto run = run_of(found);
    if (verbose_) {
        const auto miss = found.value_ ? cache::Miss::corrupt : found.miss_;
        std::cerr << "[cache] " << (run ? "hit" : "miss") << ' ' << where.kind_ << ' '
                  << project::path_text(where.directory_ / project::native_path(where.file_))
                  << (run ? "" : ": " + std::string(cache::miss_text(miss))) << '\n';
    }
    return run;
}

void TransformCache::store(const transforms::HostQuery &query, const transforms::HostRun &run) {
    store_.put(address(query), key(query), {RUN_FORMAT, {{"reply", run.reply_}, {"output", run.output_}}});
}

cache::Key TransformCache::key(const transforms::HostQuery &query) {
    cache::KeyBuilder key;
    key.add(CLAUSE_VERSION).add(cache::program_identity()).add(query.kind_).add(query.bytes_);
    // The transform host's path names its identity (plan 21 LIB77A).
    const auto &runner = query.request_.erl_;
    key.add(query.request_.clause_host_ ? project::path_text(runner) : erl_identity(runner));
    for (const auto &source : query.request_.sources_) {
        key.add_file(source.path_);
        add_includes(key, sources_.get(), source.path_);
    }
    for (const auto &directory : query.request_.code_paths_) {
        key.add(beam_digest(directory));
    }
    return key.finish();
}

const std::string &TransformCache::erl_identity(const fs::path &erl) {
    auto &identity = erls_[erl];
    if (identity.empty()) {
        std::error_code error;
        const auto root = fs::weakly_canonical(erl, error).parent_path().parent_path();
        cache::KeyBuilder key;
        key.add_file(erl);
        for (const auto &release : files_with(root / "releases", "")) {
            key.add_file(release / "OTP_VERSION");
        }
        identity = key.finish();
    }
    return identity;
}

const std::string &TransformCache::beam_digest(const fs::path &directory) {
    auto &digest = beams_[directory];
    if (digest.empty()) {
        cache::KeyBuilder key;
        for (const auto &beam : files_with(directory, ".beam")) {
            key.add_file(beam);
        }
        digest = key.finish();
    }
    return digest;
}
} // namespace clause::cli
