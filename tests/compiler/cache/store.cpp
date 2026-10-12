// Added for the SQLite cache backend (plan step 62D5): the store under threads and processes sharing one database,
// damaged databases and blob files, prune, and the timings that set the inline limit.
//   cache_store threads|damage|prune|bench ROOT
//   cache_store write ROOT ID COUNT     (one of several processes writing at once)
//   cache_store check ROOT IDS COUNT    (every process's entries are there)
#include "cache/store.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;
using clause::cache::Address;
using clause::cache::Miss;
using clause::cache::StoreOptions;
using clause::cache::Value;

// Small values stay in the database; large ones go to blob files under this test's inline limit.
constexpr std::size_t INLINE_LIMIT = 4096;
constexpr std::size_t SMALL = 1000;
constexpr std::size_t LARGE = 6000;
constexpr std::uint32_t FORMAT = 7;

void require(const bool condition, const std::string &what) {
    if (!condition) {
        throw std::runtime_error(what);
    }
}

// Deterministic bytes for one entry, so any process can check another's values.
std::string bytes_of(const std::string &name, const std::size_t size) {
    std::string out(size, '\0');
    std::uint64_t state = std::hash<std::string>{}(name) | 1U;
    for (auto &byte : out) {
        state ^= state << 13U;
        state ^= state >> 7U;
        state ^= state << 17U;
        byte = static_cast<char>(state);
    }
    return out;
}

// One entry's value: a small part and, for every other entry, a large part.
Value value_of(const std::string &name, const std::size_t index) {
    Value value{FORMAT, {{"small", bytes_of(name + "/small", SMALL)}}};
    if (index % 2 == 1) {
        value.parts_.push_back({"large", bytes_of(name + "/large", LARGE)});
    }
    return value;
}

Address address_of(const fs::path &root, const std::string &file) { return {root / "shared", file, "test"}; }

std::unique_ptr<clause::cache::Store> open(const fs::path &root, StoreOptions options = {}) {
    options.inline_limit_ = INLINE_LIMIT;
    return clause::cache::open_store(root / "cache", options);
}

// Whether the store holds exactly the value `put` wrote for one entry.
bool holds(clause::cache::Store &store, const fs::path &root, const std::string &name, const std::size_t index) {
    const auto found = store.get(address_of(root, name), "key-" + name, FORMAT);
    const auto expected = value_of(name, index);
    if (!found.value_ || found.value_->parts_.size() != expected.parts_.size()) {
        return false;
    }
    for (std::size_t i = 0; i < expected.parts_.size(); ++i) {
        const auto &part = found.value_->parts_[i];
        if (part.name_ != expected.parts_[i].name_ || part.bytes_ != expected.parts_[i].bytes_) {
            return false;
        }
    }
    return true;
}

void write_entries(clause::cache::Store &store, const fs::path &root, const std::string &prefix,
                   const std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const auto name = prefix + "-" + std::to_string(i);
        store.put(address_of(root, name), "key-" + name, value_of(name, i));
        require(holds(store, root, name, i), "read back " + name);
    }
}

std::size_t count_entries(clause::cache::Store &store, const fs::path &root, const std::string &prefix,
                          const std::size_t count) {
    std::size_t found = 0;
    for (std::size_t i = 0; i < count; ++i) {
        found += holds(store, root, prefix + "-" + std::to_string(i), i) ? 1 : 0;
    }
    return found;
}

// Eight threads write and read one store at once; a new store then sees every entry.
void threads(const fs::path &root) {
    constexpr std::size_t THREADS = 8;
    constexpr std::size_t COUNT = 40;
    {
        const auto store = open(root);
        std::vector<std::jthread> workers;
        for (std::size_t t = 0; t < THREADS; ++t) {
            workers.emplace_back([&store, &root, t] { write_entries(*store, root, "t" + std::to_string(t), COUNT); });
        }
    }
    const auto store = open(root);
    for (std::size_t t = 0; t < THREADS; ++t) {
        require(count_entries(*store, root, "t" + std::to_string(t), COUNT) == COUNT, "thread entries kept");
    }
    std::cout << "cache_store: " << THREADS * COUNT << " entries written by " << THREADS << " threads kept\n";
}

void overwrite(const fs::path &file, const std::string &bytes) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

// A missing blob file, a garbage and a truncated database each read as a miss, and the store works again.
void damage(const fs::path &root) {
    const auto database = root / "cache/v2/cache.db";
    {
        const auto store = open(root);
        write_entries(*store, root, "d", 2);
        fs::remove_all(root / "cache/v2/blobs");
        require(store->get(address_of(root, "d-1"), "key-d-1", FORMAT).miss_ == Miss::corrupt, "missing blob");
        require(store->get(address_of(root, "d-1"), "key-d-1", FORMAT).miss_ == Miss::absent, "damaged entry dropped");
        require(holds(*store, root, "d-0", 0), "inline entry kept");
        write_entries(*store, root, "d", 2);
        require(store->get(address_of(root, "d-0"), "key-d-0", FORMAT + 1).miss_ == Miss::stale, "other format");
    }
    overwrite(database, std::string(4096, 'x'));
    {
        const auto store = open(root);
        require(store->get(address_of(root, "d-0"), "key-d-0", FORMAT).miss_ == Miss::absent, "garbage database");
        write_entries(*store, root, "d", 4);
    }
    const auto size = fs::file_size(database);
    fs::resize_file(database, size / 2);
    {
        const auto store = open(root);
        require(!store->get(address_of(root, "d-3"), "key-d-3", FORMAT).value_, "truncated database");
        write_entries(*store, root, "d", 4);
    }
    require(count_entries(*open(root), root, "d", 4) == 4, "rebuilt entries kept");
    std::cout << "cache_store: missing blobs, garbage and truncated databases are misses, then rebuilt\n";
}

// prune drops entries unused for longer than the maximum age, orphan blob files, and the old v1 tree.
void prune(const fs::path &root) {
    StoreOptions options;
    options.max_age_ = std::chrono::seconds(0);
    options.orphan_age_ = std::chrono::seconds(0);
    fs::create_directories(root / "cache/v1/src-00000000");
    overwrite(root / "cache/v1/src-00000000/index", "old");
    {
        const auto store = open(root, options);
        write_entries(*store, root, "p", 2);
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        store->prune();
        require(store->get(address_of(root, "p-1"), "key-p-1", FORMAT).miss_ == Miss::absent, "unused entry kept");
    }
    require(fs::is_empty(root / "cache/v2/blobs"), "orphan blob files kept");
    require(!fs::exists(root / "cache/v1"), "v1 tree kept");
    std::cout << "cache_store: prune drops unused entries, orphan blobs and the v1 tree\n";
}

void write(const fs::path &root, const std::string &id, const std::size_t count) {
    write_entries(*open(root), root, "w" + id, count);
}

void check(const fs::path &root, const std::size_t ids, const std::size_t count) {
    const auto store = open(root);
    std::size_t found = 0;
    for (std::size_t id = 0; id < ids; ++id) {
        found += count_entries(*store, root, "w" + std::to_string(id), count);
    }
    std::cout << "cache_store: " << found << " of " << ids * count << " entries of concurrent processes kept\n";
    require(found == ids * count, "process entries kept");
}

double milliseconds_since(const std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Put `count` different values of `size` bytes with a fresh store, then open another and get them all.
void time_values(const fs::path &root, const std::size_t count, const std::size_t size, const std::size_t limit) {
    fs::remove_all(root / "cache");
    StoreOptions options;
    options.inline_limit_ = limit;
    const auto name = [](const std::size_t i) { return "m" + std::to_string(i) + ".erl"; };
    std::vector<std::string> values;
    for (std::size_t i = 0; i < count; ++i) {
        values.push_back(bytes_of(name(i), size));
    }
    auto start = std::chrono::steady_clock::now();
    {
        const auto store = clause::cache::open_store(root / "cache", options);
        for (std::size_t i = 0; i < count; ++i) {
            store->put({root / "src", name(i), "bench"}, "k", Value{1, {{"value", values[i]}}});
        }
    }
    const auto put = milliseconds_since(start);
    start = std::chrono::steady_clock::now();
    std::size_t hits = 0;
    {
        const auto store = clause::cache::open_store(root / "cache", options);
        for (std::size_t i = 0; i < count; ++i) {
            const auto found = store->get({root / "src", name(i), "bench"}, "k", 1);
            hits += found.value_ && found.value_->parts_.at(0).bytes_ == values[i] ? 1 : 0;
        }
    }
    std::cout << count << " x " << size << " B (" << (size > limit ? "blob files" : "in the database") << "): put "
              << put << " ms, open + get " << milliseconds_since(start) << " ms, " << hits << " hits\n";
}

// Timings recorded in docs/projects.md#build-caches; not a test.
void bench(const fs::path &root) {
    constexpr std::size_t SMALL_VALUE = 8192;
    constexpr std::size_t HUGE_VALUE = std::size_t{24} << 20U;
    constexpr std::size_t DEFAULT_LIMIT = std::size_t{256} << 10U;
    time_values(root, 200, SMALL_VALUE, DEFAULT_LIMIT);
    time_values(root, 2000, SMALL_VALUE, DEFAULT_LIMIT);
    time_values(root, 3, HUGE_VALUE, DEFAULT_LIMIT);
    time_values(root, 3, HUGE_VALUE, HUGE_VALUE);
    for (const std::size_t size :
         {std::size_t{64} << 10U, std::size_t{256} << 10U, std::size_t{1} << 20U, std::size_t{4} << 20U}) {
        time_values(root, 20, size, size);
        time_values(root, 20, size, size - 1);
    }
}

int run(const std::span<char *const> arguments) {
    const std::string mode = arguments.size() > 2 ? arguments[1] : "";
    const fs::path root = arguments.size() > 2 ? fs::absolute(arguments[2]) : fs::path();
    if (mode == "threads" || mode == "damage" || mode == "prune") {
        fs::remove_all(root);
        (mode == "threads" ? threads : mode == "damage" ? damage : prune)(root);
    } else if (mode == "bench") {
        bench(root);
        return 0;
    } else if ((mode == "write" || mode == "check") && arguments.size() == 5) {
        const auto count = std::stoul(arguments[4]);
        mode == "write" ? write(root, arguments[3], count) : check(root, std::stoul(arguments[3]), count);
        if (mode == "write") {
            return 0;
        }
    } else {
        std::cerr << "usage: cache_store threads|damage|prune|bench ROOT, write ROOT ID COUNT, check ROOT IDS COUNT\n";
        return 2;
    }
    std::cout << "cache_store: ok\n";
    return 0;
}
} // namespace

int main(int argc, char **argv) {
    try {
        return run(std::span<char *const>(argv, static_cast<std::size_t>(argc)));
    } catch (const std::exception &error) {
        std::cerr << "cache_store: " << error.what() << '\n';
        return 1;
    }
}
