#pragma once
// Added for the SQLite cache backend (plan step 62D5): small owners of a SQLite connection and its statements.
// Failed calls throw sqlite::Error; callers turn every failure into a cache miss.
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace clause::cache::sqlite {
// A failed SQLite call: its primary result code and SQLite's message.
class Error : public std::runtime_error {
  public:
    Error(int code, const std::string &message) : std::runtime_error(message), code_(code) {}

    // A file that holds another program's or format's database, treated like a damaged one.
    static Error foreign(const std::string &message);
    // A database this file system cannot keep (no journal mode works).
    static Error unusable(const std::string &message);

    // Whether the file is damaged or not a database at all, so it must be moved aside and recreated.
    [[nodiscard]] bool damaged() const;

  private:
    int code_;
};

// One prepared statement; parameters are numbered from 1 and columns from 0.
class Statement {
  public:
    Statement(sqlite3 *database, const std::string &sql);
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    ~Statement();

    Statement &bind(int index, const std::string &text);
    Statement &bind(int index, std::int64_t value);
    Statement &bind_blob(int index, std::string_view bytes);
    Statement &bind_null(int index);

    // Advance to the next row; false once the statement is done.
    bool step();

    [[nodiscard]] std::string text(int column) const;
    [[nodiscard]] std::string blob(int column) const;
    [[nodiscard]] std::int64_t integer(int column) const;
    [[nodiscard]] bool null(int column) const;

  private:
    // Throw for a result code other than `ok`.
    void check(int result) const;

    sqlite3 *database_;
    sqlite3_stmt *statement_ = nullptr;
};

// One open connection to a database file, created when missing.
class Database {
  public:
    explicit Database(const std::filesystem::path &file);
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;
    ~Database();

    // Wait up to `milliseconds` for another connection's lock before a call reports busy.
    void busy_timeout(int milliseconds);
    // Run statements that return nothing the caller needs.
    void execute(const std::string &sql);
    // The first column of the first row of `sql`, as text (empty when there is no row).
    std::string query_text(const std::string &sql);
    [[nodiscard]] std::int64_t last_insert_id() const;

    [[nodiscard]] sqlite3 *handle() const { return database_; }

  private:
    sqlite3 *database_ = nullptr;
};

// A transaction that rolls back unless committed. `immediate` takes the write lock at once, so a writer waits
// in the busy timeout instead of failing when another writer got there first.
class Transaction {
  public:
    Transaction(Database &database, bool immediate);
    Transaction(const Transaction &) = delete;
    Transaction &operator=(const Transaction &) = delete;
    ~Transaction();

    void commit();

  private:
    Database &database_;
    bool open_ = true;
};
} // namespace clause::cache::sqlite
