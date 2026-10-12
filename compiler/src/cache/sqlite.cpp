// Added for the SQLite cache backend (plan step 62D5).
#include "sqlite.hpp"
#include <sqlite3.h>

namespace clause::cache::sqlite {
namespace {
// Throw the connection's last error for a failed result.
[[noreturn]] void fail(sqlite3 *database, const int result) {
    throw Error(result & 0xFF, database ? sqlite3_errmsg(database) : sqlite3_errstr(result));
}

int length(const std::string_view bytes) { return static_cast<int>(bytes.size()); }
} // namespace

Error Error::foreign(const std::string &message) { return {SQLITE_NOTADB, message}; }

Error Error::unusable(const std::string &message) { return {SQLITE_CANTOPEN, message}; }

bool Error::damaged() const { return code_ == SQLITE_CORRUPT || code_ == SQLITE_NOTADB; }

Statement::Statement(sqlite3 *database, const std::string &sql) : database_(database) {
    check(sqlite3_prepare_v2(database_, sql.c_str(), length(sql), &statement_, nullptr));
}

Statement::~Statement() { sqlite3_finalize(statement_); }

void Statement::check(const int result) const {
    if (result != SQLITE_OK) {
        fail(database_, result);
    }
}

Statement &Statement::bind(const int index, const std::string &text) {
    check(sqlite3_bind_text(statement_, index, text.c_str(), length(text), SQLITE_TRANSIENT));
    return *this;
}

Statement &Statement::bind(const int index, const std::int64_t value) {
    check(sqlite3_bind_int64(statement_, index, value));
    return *this;
}

Statement &Statement::bind_blob(const int index, const std::string_view bytes) {
    // A zero-length blob still binds as a blob, never as NULL.
    check(bytes.empty() ? sqlite3_bind_zeroblob(statement_, index, 0)
                        : sqlite3_bind_blob(statement_, index, bytes.data(), length(bytes), SQLITE_TRANSIENT));
    return *this;
}

Statement &Statement::bind_null(const int index) {
    check(sqlite3_bind_null(statement_, index));
    return *this;
}

bool Statement::step() {
    const auto result = sqlite3_step(statement_);
    if (result != SQLITE_ROW && result != SQLITE_DONE) {
        fail(database_, result);
    }
    return result == SQLITE_ROW;
}

std::string Statement::text(const int column) const {
    const auto *data = sqlite3_column_text(statement_, column);
    return data ? std::string(reinterpret_cast<const char *>(data),
                              static_cast<std::size_t>(sqlite3_column_bytes(statement_, column)))
                : std::string();
}

std::string Statement::blob(const int column) const {
    const auto *data = static_cast<const char *>(sqlite3_column_blob(statement_, column));
    return data ? std::string(data, static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))) : std::string();
}

std::int64_t Statement::integer(const int column) const { return sqlite3_column_int64(statement_, column); }

bool Statement::null(const int column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }

Database::Database(const std::filesystem::path &file) {
    const auto name = file.u8string();
    const auto result = sqlite3_open_v2(reinterpret_cast<const char *>(name.c_str()), &database_,
                                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (result != SQLITE_OK) {
        Error error(result & 0xFF, database_ ? sqlite3_errmsg(database_) : sqlite3_errstr(result));
        sqlite3_close(database_);
        throw error;
    }
}

Database::~Database() { sqlite3_close(database_); }

void Database::busy_timeout(const int milliseconds) { sqlite3_busy_timeout(database_, milliseconds); }

void Database::execute(const std::string &sql) {
    if (const auto result = sqlite3_exec(database_, sql.c_str(), nullptr, nullptr, nullptr); result != SQLITE_OK) {
        fail(database_, result);
    }
}

std::string Database::query_text(const std::string &sql) {
    Statement statement(database_, sql);
    return statement.step() ? statement.text(0) : std::string();
}

std::int64_t Database::last_insert_id() const { return sqlite3_last_insert_rowid(database_); }

Transaction::Transaction(Database &database, const bool immediate) : database_(database) {
    database_.execute(immediate ? "BEGIN IMMEDIATE" : "BEGIN");
}

Transaction::~Transaction() {
    if (open_) {
        sqlite3_exec(database_.handle(), "ROLLBACK", nullptr, nullptr, nullptr);
    }
}

void Transaction::commit() {
    database_.execute("COMMIT");
    open_ = false;
}
} // namespace clause::cache::sqlite
