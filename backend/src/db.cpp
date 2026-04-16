#include "db.hpp"

#include <cstring>

namespace {

bool exec_sql(sqlite3* db, const char* sql) {
  char* err = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    if (err) {
      sqlite3_free(err);
    }
    return false;
  }
  return true;
}

}  // namespace

UserStore::UserStore(std::string db_path) : db_path_(std::move(db_path)) {}

UserStore::~UserStore() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool UserStore::open() {
  if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK) {
    return false;
  }
  const char* schema = R"SQL(
    CREATE TABLE IF NOT EXISTS users (
      token TEXT PRIMARY KEY
    );
    CREATE TABLE IF NOT EXISTS known_words (
      token TEXT NOT NULL,
      word_id TEXT NOT NULL,
      PRIMARY KEY (token, word_id)
    );
  )SQL";
  return exec_sql(db_, schema);
}

void UserStore::ensure_user(const std::string& token) {
  if (token.empty() || !db_) {
    return;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "INSERT OR IGNORE INTO users(token) VALUES (?1)";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return;
  }
  sqlite3_bind_text(stmt, 1, token.c_str(), static_cast<int>(token.size()), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

bool UserStore::add_known(const std::string& token, const std::string& word_id) {
  if (token.empty() || word_id.empty() || !db_) {
    return false;
  }
  ensure_user(token);
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "INSERT OR IGNORE INTO known_words(token, word_id) VALUES (?1, ?2)";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, token.c_str(), static_cast<int>(token.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, word_id.c_str(), static_cast<int>(word_id.size()), SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

std::unordered_set<std::string> UserStore::known_ids(const std::string& token) const {
  std::unordered_set<std::string> out;
  if (!db_ || token.empty()) {
    return out;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT word_id FROM known_words WHERE token = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return out;
  }
  sqlite3_bind_text(stmt, 1, token.c_str(), static_cast<int>(token.size()), SQLITE_TRANSIENT);
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (text) {
      out.emplace(text);
    }
  }
  sqlite3_finalize(stmt);
  return out;
}
