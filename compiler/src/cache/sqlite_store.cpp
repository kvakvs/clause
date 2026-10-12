// Added for the SQLite cache backend (plan step 62D5): entries and their parts in `v2/cache.db`, parts above the
// inline limit in content-addressed blob files beside it (docs/projects.md#build-caches).
#include "blob_files.hpp"
#include "sqlite.hpp"
#include "store.hpp"
#include <llvm/Support/xxhash.h>
#include <mutex>
#include <ostream>

namespace clause::cache {
namespace {
namespace fs = std::filesystem;

// The on-disk format: another format lives in another v<N> directory. v1 was the file backend of step 62D2.
constexpr std::string_view FORMAT_DIRECTORY = "v2";
constexpr std::string_view LEGACY_DIRECTORY = "v1";
// The database's application id ("Clau") and schema version; a database with others is moved aside.
constexpr std::string_view APPLICATION_ID = "1131176309";
constexpr std::string_view SCHEMA_VERSION = "1";
constexpr std::string_view SCHEMA = R"sql(
CREATE TABLE entries(
    id INTEGER PRIMARY KEY,
    directory TEXT NOT NULL,
    file TEXT NOT NULL,
    kind TEXT NOT NULL,
    key TEXT NOT NULL,
    format INTEGER NOT NULL,
    used INTEGER NOT NULL);
CREATE UNIQUE INDEX entries_address ON entries(directory, file, kind);
CREATE TABLE parts(
    entry INTEGER NOT NULL,
    ordinal INTEGER NOT NULL,
    name TEXT NOT NULL,
    size INTEGER NOT NULL,
    checksum INTEGER NOT NULL,
    bytes BLOB,
    blob TEXT,
    PRIMARY KEY(entry, ordinal));
PRAGMA application_id = 1131176309;
PRAGMA user_version = 1;
)sql";
// How long a writer waits for another process's lock, and how stale a last-use time may get before a hit updates it.
constexpr int BUSY_TIMEOUT_MS = 30'000;
constexpr std::int64_t TOUCH_INTERVAL_SECONDS = std::int64_t{24} * 60 * 60;

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// A part's checksum, stored as SQLite's signed integer.
std::int64_t checksum(const std::string_view bytes) {
    return static_cast<std::int64_t>(
        llvm::xxh3_64bits(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
}

// An input directory as entries name it: absolute, normalized, with `/` separators.
std::string directory_text(const fs::path &directory) {
    std::error_code error;
    const auto text = fs::absolute(directory, error).lexically_normal().generic_u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}

// An entry row: its id, key, value format and last use.
struct Row {
    std::int64_t id_ = 0;
    Key key_;
    std::int64_t format_ = 0;
    std::int64_t used_ = 0;
};

class SqliteStore final : public Store {
  public:
    SqliteStore(const fs::path &root, const StoreOptions &options)
        : directory_(root / FORMAT_DIRECTORY), legacy_(root / LEGACY_DIRECTORY), options_(options),
          blobs_(directory_ / "blobs") {}

    Lookup get(const Address &address, const Key &key, const std::uint32_t format) override {
        const std::scoped_lock lock(mutex_);
        if (!ready(false)) {
            return {};
        }
        try {
            return lookup(address, key, format);
        } catch (const sqlite::Error &error) {
            recover(error);
            return {std::nullopt, Miss::corrupt};
        }
    }

    void put(const Address &address, const Key &key, const Value &value) override {
        const std::scoped_lock lock(mutex_);
        if (!ready(true)) {
            return;
        }
        try {
            store(address, key, value);
        } catch (const sqlite::Error &error) {
            recover(error);
        }
    }

    void invalidate(const Address &address) override {
        const std::scoped_lock lock(mutex_);
        if (!ready(false)) {
            return;
        }
        try {
            sqlite::Transaction write(*database_, true);
            erase(address);
            write.commit();
        } catch (const sqlite::Error &error) {
            recover(error);
        }
    }

    void prune() override {
        const std::scoped_lock lock(mutex_);
        std::error_code error;
        fs::remove_all(legacy_, error);
        if (!database_ || !written_) {
            return;
        }
        try {
            drop_unused();
            blobs_.remove_unnamed(named_blobs(), options_.orphan_age_);
            database_->execute("PRAGMA incremental_vacuum");
        } catch (const sqlite::Error &failure) {
            recover(failure);
        }
    }

  private:
    // Open the database on first use. Only a write creates it, so runs that only look leave no files behind; after a
    // failed open the cache stays off for the run.
    bool ready(const bool create) {
        if (!database_ && !disabled_ && (create || fs::exists(directory_ / "cache.db"))) {
            connect();
            disabled_ = !database_;
        }
        return database_ != nullptr;
    }

    // Open the database. One found damaged or foreign twice in a row (another process may replace it in between) is
    // moved aside and created anew; when that fails too, the cache is off for this run.
    void connect() {
        for (int attempt = 0; attempt < 3 && !database_; ++attempt) {
            try {
                open();
            } catch (const sqlite::Error &error) {
                const bool retry = error.damaged() && (attempt == 0 || (attempt == 1 && move_aside()));
                if (!retry) {
                    note(std::string("build cache disabled: ") + error.what());
                    return;
                }
            }
        }
    }

    void open() {
        std::error_code error;
        fs::create_directories(directory_, error);
        auto database = std::make_unique<sqlite::Database>(directory_ / "cache.db");
        database->busy_timeout(BUSY_TIMEOUT_MS);
        // Freed pages return to the file system on prune; this only takes effect on a new, empty file.
        database->execute("PRAGMA auto_vacuum = INCREMENTAL");
        choose_journal(*database);
        database->execute("PRAGMA synchronous = NORMAL");
        create_schema(*database);
        database_ = std::move(database);
    }

    // Write-ahead logging lets readers run beside a writer, but needs shared memory, which network file systems lack.
    void choose_journal(sqlite::Database &database) {
        if (database.query_text("PRAGMA journal_mode = WAL") == "wal") {
            return;
        }
        if (database.query_text("PRAGMA journal_mode = DELETE") != "delete") {
            throw sqlite::Error::unusable("no journal mode works in " + directory_.string());
        }
        note("build cache uses a rollback journal: write-ahead logging is not available here");
    }

    // Create the tables of a new database; a database of another program or schema is foreign.
    static void create_schema(sqlite::Database &database) {
        sqlite::Transaction setup(database, true);
        const auto application = database.query_text("PRAGMA application_id");
        const auto version = database.query_text("PRAGMA user_version");
        if (application == "0" && version == "0") {
            database.execute(std::string(SCHEMA));
        } else if (application != APPLICATION_ID || version != SCHEMA_VERSION) {
            throw sqlite::Error::foreign("not a cache database of this clau");
        }
        setup.commit();
    }

    // Rename a damaged database out of the way (replacing an older one) and drop its journal files.
    bool move_aside() {
        database_.reset();
        const auto file = directory_ / "cache.db";
        std::error_code error;
        fs::rename(file, directory_ / "cache.db.damaged", error);
        if (error) {
            return false;
        }
        for (const auto *suffix : {"-wal", "-shm", "-journal"}) {
            fs::remove(directory_ / ("cache.db" + std::string(suffix)), error);
        }
        note("build cache: moved a damaged cache.db aside");
        return true;
    }

    // After a failed call: a damaged database is replaced, anything else (busy, full, I/O) is only a miss.
    void recover(const sqlite::Error &error) {
        if (error.damaged() && move_aside()) {
            connect();
            disabled_ = !database_;
        }
    }

    void note(const std::string &text) const {
        if (options_.notes_) {
            *options_.notes_ << "[cache] " << text << '\n';
        }
    }

    Lookup lookup(const Address &address, const Key &key, const std::uint32_t format) {
        std::optional<std::vector<Part>> parts;
        Row row;
        {
            // One read transaction, so the entry and its parts come from the same snapshot.
            sqlite::Transaction read(*database_, false);
            auto found = find(address);
            if (!found) {
                return {std::nullopt, Miss::absent};
            }
            if (found->key_ != key || found->format_ != format) {
                return {std::nullopt, Miss::stale};
            }
            row = std::move(*found);
            parts = read_parts(row.id_);
            read.commit();
        }
        if (!parts) {
            sqlite::Transaction write(*database_, true);
            erase(address);
            write.commit();
            return {std::nullopt, Miss::corrupt};
        }
        touch(row);
        return {Value{format, std::move(*parts)}, Miss::none};
    }

    std::optional<Row> find(const Address &address) {
        sqlite::Statement select(database_->handle(), "SELECT id, key, format, used FROM entries "
                                                      "WHERE directory = ?1 AND file = ?2 AND kind = ?3");
        select.bind(1, directory_text(address.directory_)).bind(2, address.file_).bind(3, address.kind_);
        if (!select.step()) {
            return std::nullopt;
        }
        return Row{select.integer(0), select.text(1), select.integer(2), select.integer(3)};
    }

    // An entry's parts in order; none when one fails its size or checksum, or its blob file is gone.
    std::optional<std::vector<Part>> read_parts(const std::int64_t entry) {
        sqlite::Statement select(database_->handle(), "SELECT name, size, checksum, bytes, blob FROM parts "
                                                      "WHERE entry = ?1 ORDER BY ordinal");
        select.bind(1, entry);
        std::vector<Part> parts;
        while (select.step()) {
            const auto size = static_cast<std::uint64_t>(select.integer(1));
            auto bytes = select.null(4) ? std::optional(select.blob(3)) : blobs_.read(select.text(4), size);
            if (!bytes || bytes->size() != size || checksum(*bytes) != select.integer(2)) {
                return std::nullopt;
            }
            parts.push_back({select.text(0), std::move(*bytes)});
        }
        return parts;
    }

    // Record a hit's time, at most once per interval, so hits rarely write.
    void touch(const Row &row) {
        const auto now = now_seconds();
        if (now - row.used_ < TOUCH_INTERVAL_SECONDS) {
            return;
        }
        sqlite::Statement update(database_->handle(), "UPDATE entries SET used = ?2 WHERE id = ?1");
        update.bind(1, row.id_).bind(2, now).step();
    }

    // Write large parts' blob files first, then replace the entry in one transaction: a row never names a file
    // that is not complete, and a crash in between leaves only an orphan file for prune.
    void store(const Address &address, const Key &key, const Value &value) {
        std::vector<std::optional<std::string>> files;
        for (const auto &part : value.parts_) {
            files.emplace_back();
            if (part.bytes_.size() > options_.inline_limit_) {
                files.back() = hash_text(part.bytes_);
                if (!blobs_.write(*files.back(), part.bytes_)) {
                    return;
                }
            }
        }
        sqlite::Transaction write(*database_, true);
        erase(address);
        sqlite::Statement insert(database_->handle(), "INSERT INTO entries(directory, file, kind, key, format, used) "
                                                      "VALUES(?1, ?2, ?3, ?4, ?5, ?6)");
        insert.bind(1, directory_text(address.directory_)).bind(2, address.file_).bind(3, address.kind_);
        insert.bind(4, key).bind(5, std::int64_t{value.format_}).bind(6, now_seconds()).step();
        const auto entry = database_->last_insert_id();
        for (std::size_t i = 0; i < value.parts_.size(); ++i) {
            insert_part(entry, static_cast<std::int64_t>(i), value.parts_[i], files[i]);
        }
        write.commit();
        written_ = true;
    }

    void insert_part(const std::int64_t entry, const std::int64_t ordinal, const Part &part,
                     const std::optional<std::string> &file) {
        sqlite::Statement insert(database_->handle(), "INSERT INTO parts(entry, ordinal, name, size, checksum, "
                                                      "bytes, blob) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
        insert.bind(1, entry).bind(2, ordinal).bind(3, part.name_);
        insert.bind(4, static_cast<std::int64_t>(part.bytes_.size())).bind(5, checksum(part.bytes_));
        if (file) {
            insert.bind_null(6).bind(7, *file);
        } else {
            insert.bind_blob(6, part.bytes_).bind_null(7);
        }
        insert.step();
    }

    // Delete the entry of an address with its parts; the caller holds a write transaction.
    void erase(const Address &address) {
        const auto directory = directory_text(address.directory_);
        sqlite::Statement parts(database_->handle(), "DELETE FROM parts WHERE entry IN (SELECT id FROM entries "
                                                     "WHERE directory = ?1 AND file = ?2 AND kind = ?3)");
        parts.bind(1, directory).bind(2, address.file_).bind(3, address.kind_).step();
        sqlite::Statement entries(database_->handle(),
                                  "DELETE FROM entries WHERE directory = ?1 AND file = ?2 AND kind = ?3");
        entries.bind(1, directory).bind(2, address.file_).bind(3, address.kind_).step();
    }

    // Delete entries not used within the configured age, with their parts.
    void drop_unused() {
        const auto cutoff = now_seconds() - options_.max_age_.count();
        sqlite::Transaction write(*database_, true);
        sqlite::Statement parts(database_->handle(),
                                "DELETE FROM parts WHERE entry IN (SELECT id FROM entries WHERE used < ?1)");
        parts.bind(1, cutoff).step();
        sqlite::Statement entries(database_->handle(), "DELETE FROM entries WHERE used < ?1");
        entries.bind(1, cutoff).step();
        write.commit();
    }

    std::set<std::string> named_blobs() {
        sqlite::Statement select(database_->handle(), "SELECT DISTINCT blob FROM parts WHERE blob IS NOT NULL");
        std::set<std::string> names;
        while (select.step()) {
            names.insert(select.text(0));
        }
        return names;
    }

    // The format directory and the 62D2 file backend's directory, which prune removes.
    fs::path directory_;
    fs::path legacy_;
    StoreOptions options_;
    BlobFiles blobs_;
    // The connection once opened, and whether opening failed, so the cache is off for this run.
    std::unique_ptr<sqlite::Database> database_;
    bool disabled_ = false;
    // Whether this store wrote anything, so prune has work to do.
    bool written_ = false;
    // Serializes every operation of the threads sharing this store.
    std::mutex mutex_;
};
} // namespace

std::unique_ptr<Store> open_store(const fs::path &root, const StoreOptions &options) {
    return std::make_unique<SqliteStore>(root, options);
}
} // namespace clause::cache
