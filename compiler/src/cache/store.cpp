// Added for build caches (plan step 62D2): the file backend of the cache store.
#include "store.hpp"
#include <array>
#include <chrono>
#include <fstream>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/xxhash.h>
#include <map>
#include <random>
#include <set>
#include <sstream>

namespace clause::cache {
namespace {
namespace fs = std::filesystem;

// The on-disk format; another format lives in another v<N> directory and is never read.
constexpr std::string_view FORMAT_DIRECTORY = "v1";
constexpr std::string_view INDEX_MAGIC = "clause-cache-index-1\n";
constexpr std::uint32_t RECORD_MAGIC = 0x31434143U;
// Compact a directory's packs when they hold this many files, or more than twice the live bytes plus this slack.
constexpr std::size_t PACK_FILES_LIMIT = 8;
constexpr std::uint64_t DEAD_BYTES_SLACK = std::uint64_t{1} << 20U;

std::uint64_t checksum(std::string_view bytes) {
    return llvm::xxh3_64bits(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
}

// Little-endian fixed-width integers and length-prefixed strings of the index and pack records.
void put_number(std::string &out, std::uint64_t value) {
    for (unsigned shift = 0; shift < 64; shift += 8) {
        out.push_back(static_cast<char>((value >> shift) & 0xFFU));
    }
}

void put_text(std::string &out, std::string_view text) {
    put_number(out, text.size());
    out.append(text);
}

class Reader {
  public:
    explicit Reader(std::string_view bytes) : bytes_(bytes) {}

    std::optional<std::uint64_t> number() {
        if (bytes_.size() < 8) {
            return std::nullopt;
        }
        std::uint64_t value = 0;
        for (unsigned i = 0; i < 8; ++i) {
            value |= std::uint64_t{static_cast<unsigned char>(bytes_[i])} << (8 * i);
        }
        bytes_.remove_prefix(8);
        return value;
    }

    std::optional<std::string> text() {
        const auto size = number();
        if (!size || *size > bytes_.size()) {
            return std::nullopt;
        }
        std::string result(bytes_.substr(0, *size));
        bytes_.remove_prefix(*size);
        return result;
    }

    bool empty() const { return bytes_.empty(); }

  private:
    // What is left to read.
    std::string_view bytes_;
};

struct Entry {
    // The key the result was stored under, where its record lies, and its payload's checksum.
    Key key_;
    std::string pack_;
    std::uint64_t offset_ = 0;
    std::uint64_t size_ = 0;
    std::uint64_t checksum_ = 0;
};

// Entries by (file name, result kind).
using Entries = std::map<std::pair<std::string, std::string>, Entry>;

std::optional<std::string> read_all(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

// A random name part, so concurrent clau processes never write the same pack or temporary index.
std::string unique_suffix() {
    static std::mt19937_64 generator{
        std::random_device{}() ^
        static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())};
    static constexpr std::string_view DIGITS = "0123456789abcdef";
    auto value = generator();
    std::string result(16, '0');
    for (auto &digit : result) {
        digit = DIGITS[value & 0xFU];
        value >>= 4U;
    }
    return result;
}

std::string encode_entry(const std::pair<std::string, std::string> &name, const Entry &entry) {
    std::string out;
    put_text(out, name.first);
    put_text(out, name.second);
    put_text(out, entry.key_);
    put_text(out, entry.pack_);
    put_number(out, entry.offset_);
    put_number(out, entry.size_);
    put_number(out, entry.checksum_);
    return out;
}

std::optional<std::pair<std::pair<std::string, std::string>, Entry>> decode_entry(Reader &reader) {
    auto file = reader.text();
    auto kind = reader.text();
    auto key = reader.text();
    auto pack = reader.text();
    const auto offset = reader.number();
    const auto size = reader.number();
    const auto sum = reader.number();
    if (!file || !kind || !key || !pack || !offset || !size || !sum) {
        return std::nullopt;
    }
    return std::pair{std::pair{std::move(*file), std::move(*kind)},
                     Entry{std::move(*key), std::move(*pack), *offset, *size, *sum}};
}

// The index body followed by its checksum; a damaged or foreign index reads as empty.
std::string encode_index(const Entries &entries) {
    std::string body;
    for (const auto &[name, entry] : entries) {
        body += encode_entry(name, entry);
    }
    std::string out(INDEX_MAGIC);
    put_text(out, body);
    put_number(out, checksum(body));
    return out;
}

Entries decode_index(std::string_view bytes) {
    if (!bytes.starts_with(INDEX_MAGIC)) {
        return {};
    }
    Reader outer(bytes.substr(INDEX_MAGIC.size()));
    const auto body = outer.text();
    const auto sum = outer.number();
    if (!body || !sum || *sum != checksum(*body) || !outer.empty()) {
        return {};
    }
    Entries entries;
    Reader reader(*body);
    while (!reader.empty()) {
        auto entry = decode_entry(reader);
        if (!entry) {
            return {};
        }
        entries.insert_or_assign(std::move(entry->first), std::move(entry->second));
    }
    return entries;
}

// A pack record: magic, key, payload and the payload's checksum.
std::string encode_record(const Key &key, std::string_view payload) {
    std::string out;
    put_number(out, RECORD_MAGIC);
    put_text(out, key);
    put_text(out, payload);
    put_number(out, checksum(payload));
    return out;
}

// The payload of the record `entry` names, when the record is intact and matches the entry.
std::optional<std::string> read_record(const fs::path &directory, const Entry &entry) {
    std::ifstream file(directory / entry.pack_, std::ios::binary);
    // Magic, key and payload with their lengths, and the checksum: four numbers.
    const auto length = (4 * sizeof(std::uint64_t)) + entry.key_.size() + entry.size_;
    std::string bytes(length, '\0');
    if (!file || !file.seekg(static_cast<std::streamoff>(entry.offset_)) ||
        !file.read(bytes.data(), static_cast<std::streamsize>(length))) {
        return std::nullopt;
    }
    Reader reader(bytes);
    const auto magic = reader.number();
    const auto key = reader.text();
    auto payload = reader.text();
    const auto sum = reader.number();
    const bool intact = magic == RECORD_MAGIC && key == entry.key_ && payload && sum == entry.checksum_ &&
                        checksum(*payload) == entry.checksum_;
    return intact ? payload : std::nullopt;
}

// Replace a file atomically: write a uniquely named temporary beside it, then rename it over the old one.
bool replace_file(const fs::path &path, std::string_view bytes) {
    const auto temporary = path.parent_path() / (path.filename().string() + "-" + unique_suffix() + ".tmp");
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
            return false;
        }
    }
    std::error_code error;
    fs::rename(temporary, path, error);
    if (error) {
        fs::remove(temporary, error);
        return false;
    }
    return true;
}

// The size of a file; 0 when it cannot be read.
std::uint64_t size_of(const fs::path &path) {
    const auto text = path.u8string();
    std::uint64_t size = 0;
    return llvm::sys::fs::file_size(std::string(text.begin(), text.end()), size) ? 0 : size;
}

// A path character kept in a cache directory name: ASCII letters, digits, `_`, `-` and `.`; others become `_`.
char plain(const char8_t unit) {
    const bool keep = (unit >= u8'a' && unit <= u8'z') || (unit >= u8'A' && unit <= u8'Z') ||
                      (unit >= u8'0' && unit <= u8'9') || unit == u8'_' || unit == u8'-' || unit == u8'.';
    return keep ? static_cast<char>(unit) : '_';
}

// The cache directory of one input directory: a readable tail of its path and a hash of the whole normalized path.
std::string directory_name(const fs::path &input) {
    std::error_code error;
    const auto normal = fs::absolute(input, error).lexically_normal();
    std::string tail;
    for (const auto unit : normal.filename().u8string()) {
        tail.push_back(plain(unit));
    }
    if (tail.empty()) {
        tail = "root";
    }
    const auto text = normal.generic_u8string();
    return tail.substr(0, 32) + "-" +
           hash_text({reinterpret_cast<const char *>(text.data()), text.size()}).substr(0, 8);
}

// One input directory's cache: its index (read on first use) and the packs this process appends to.
class Directory {
  public:
    explicit Directory(fs::path path) : path_(std::move(path)), entries_(load()) {}

    Lookup get(const std::pair<std::string, std::string> &name, const Key &key) const {
        const auto found = entries_.find(name);
        if (found == entries_.end()) {
            return {std::nullopt, Miss::absent};
        }
        if (found->second.key_ != key) {
            return {std::nullopt, Miss::stale};
        }
        auto payload = read_record(path_, found->second);
        return payload ? Lookup{std::move(payload), Miss::none} : Lookup{std::nullopt, Miss::corrupt};
    }

    void put(const std::pair<std::string, std::string> &name, const Key &key, std::string_view bytes) {
        std::error_code error;
        fs::create_directories(path_, error);
        auto &pack = packs_[name.second];
        if (pack.empty()) {
            pack = name.second + "-" + unique_suffix() + ".pack";
        }
        const auto record = encode_record(key, bytes);
        std::ofstream file(path_ / pack, std::ios::binary | std::ios::app);
        const auto offset = size_of(path_ / pack);
        if (!file.write(record.data(), static_cast<std::streamsize>(record.size())) || !file.flush()) {
            return;
        }
        update(name, Entry{key, pack, offset, bytes.size(), checksum(bytes)});
    }

    void invalidate(const std::pair<std::string, std::string> &name) { update(name, std::nullopt); }

    // Rewrite live records into one new pack when the packs hold mostly superseded records.
    void prune() {
        entries_ = load();
        std::uint64_t live = 0;
        for (const auto &[name, entry] : entries_) {
            live += entry.size_;
        }
        std::uint64_t total = 0;
        std::size_t files = 0;
        for (const auto &pack : pack_files()) {
            total += size_of(pack);
            ++files;
        }
        if (files > PACK_FILES_LIMIT || total > 2 * live + DEAD_BYTES_SLACK) {
            compact();
        }
    }

  private:
    Entries load() const { return decode_index(read_all(path_ / "index").value_or("")); }

    // Re-read the index (another process may have changed it), apply one change and swap the index in.
    void update(const std::pair<std::string, std::string> &name, std::optional<Entry> entry) {
        entries_ = load();
        if (entry) {
            entries_.insert_or_assign(name, std::move(*entry));
        } else {
            entries_.erase(name);
        }
        replace_file(path_ / "index", encode_index(entries_));
    }

    std::vector<fs::path> pack_files() const {
        std::vector<fs::path> result;
        std::error_code error;
        for (const auto &item : fs::directory_iterator(path_, error)) {
            if (item.path().extension() == ".pack") {
                result.push_back(item.path());
            }
        }
        return result;
    }

    // Copy every intact live record into a fresh pack, swap the index, then delete the packs it no longer names.
    void compact() {
        const auto pack = "compact-" + unique_suffix() + ".pack";
        std::string bytes;
        Entries kept;
        for (const auto &[name, entry] : entries_) {
            if (auto payload = read_record(path_, entry)) {
                kept.insert_or_assign(name, Entry{entry.key_, pack, bytes.size(), payload->size(), entry.checksum_});
                bytes += encode_record(entry.key_, *payload);
            }
        }
        if (!replace_file(path_ / pack, bytes) || !replace_file(path_ / "index", encode_index(kept))) {
            return;
        }
        entries_ = std::move(kept);
        packs_.clear();
        for (const auto &file : pack_files()) {
            std::error_code error;
            if (file.filename() != pack) {
                fs::remove(file, error);
            }
        }
    }

    fs::path path_;
    Entries entries_;
    // The pack each result kind appends to in this process.
    std::map<std::string, std::string> packs_;
};

class FileStore final : public Store {
  public:
    explicit FileStore(const fs::path &root) : root_(root / FORMAT_DIRECTORY) {}

    Lookup get(const Address &address, const Key &key) override {
        return directory(address.directory_).get({address.file_, address.kind_}, key);
    }

    void put(const Address &address, const Key &key, std::string_view bytes) override {
        directory(address.directory_).put({address.file_, address.kind_}, key, bytes);
        written_.insert(directory_name(address.directory_));
    }

    void invalidate(const Address &address) override {
        directory(address.directory_).invalidate({address.file_, address.kind_});
    }

    void prune() override {
        for (const auto &name : written_) {
            directories_.at(name).prune();
        }
    }

  private:
    Directory &directory(const fs::path &input) {
        const auto name = directory_name(input);
        auto found = directories_.find(name);
        if (found == directories_.end()) {
            found = directories_.emplace(name, Directory(root_ / name)).first;
        }
        return found->second;
    }

    fs::path root_;
    // Directory caches by name, and the names this store wrote to.
    std::map<std::string, Directory> directories_;
    std::set<std::string> written_;
};
} // namespace

std::unique_ptr<Store> file_store(const fs::path &root) { return std::make_unique<FileStore>(root); }

std::string_view miss_text(const Miss miss) {
    static constexpr std::array<std::string_view, 4> TEXTS{"hit", "no entry", "inputs changed", "damaged entry"};
    return TEXTS.at(static_cast<std::size_t>(miss));
}

std::string hash_text(std::string_view bytes) {
    const auto hash = llvm::xxh3_128bits(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    static constexpr std::string_view DIGITS = "0123456789abcdef";
    std::string result;
    result.reserve(32);
    for (const auto part : {hash.high64, hash.low64}) {
        for (int shift = 60; shift >= 0; shift -= 4) {
            result.push_back(DIGITS[(part >> static_cast<unsigned>(shift)) & 0xFU]);
        }
    }
    return result;
}

std::string program_identity() {
    static int anchor = 0;
    auto path = llvm::sys::fs::getMainExecutable(nullptr, &anchor);
    llvm::sys::fs::file_status status;
    if (llvm::sys::fs::status(path, status)) {
        return path;
    }
    const auto modified = status.getLastModificationTime().time_since_epoch().count();
    return path + "\n" + std::to_string(status.getSize()) + "\n" + std::to_string(modified);
}

KeyBuilder &KeyBuilder::add(std::string_view part) {
    put_text(bytes_, part);
    return *this;
}

KeyBuilder &KeyBuilder::add_file(const std::filesystem::path &path) {
    const auto text = path.generic_u8string();
    add({reinterpret_cast<const char *>(text.data()), text.size()});
    const auto content = read_all(path);
    return add(content ? hash_text(*content) : std::string("missing"));
}
} // namespace clause::cache
