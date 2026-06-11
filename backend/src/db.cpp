#include "auth.hpp"
#include "db.hpp"

#include <chrono>
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
    CREATE TABLE IF NOT EXISTS accounts (
      id TEXT PRIMARY KEY,
      username TEXT UNIQUE NOT NULL,
      password_hash TEXT NOT NULL,
      salt TEXT NOT NULL
    );
    CREATE TABLE IF NOT EXISTS sessions (
      token TEXT PRIMARY KEY,
      account_id TEXT NOT NULL,
      created_at INTEGER NOT NULL
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

CreateAccountOutcome UserStore::create_account(const std::string& username, const std::string& password) {
  CreateAccountOutcome out;
  if (!db_ || !auth::is_valid_username(username) || !auth::is_valid_password(password)) {
    out.result = CreateAccountResult::InvalidInput;
    return out;
  }
  const std::string account_id = auth::generate_token();
  const std::string salt = auth::generate_salt_hex();
  const std::string hash = auth::hash_password(password, salt);
  if (hash.empty()) {
    out.result = CreateAccountResult::InvalidInput;
    return out;
  }

  sqlite3_stmt* stmt = nullptr;
  const char* sql = "INSERT INTO accounts(id, username, password_hash, salt) VALUES (?1, ?2, ?3, ?4)";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    out.result = CreateAccountResult::InvalidInput;
    return out;
  }
  sqlite3_bind_text(stmt, 1, account_id.c_str(), static_cast<int>(account_id.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, username.c_str(), static_cast<int>(username.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, hash.c_str(), static_cast<int>(hash.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, salt.c_str(), static_cast<int>(salt.size()), SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_CONSTRAINT) {
    out.result = CreateAccountResult::UsernameTaken;
    return out;
  }
  if (rc != SQLITE_DONE) {
    out.result = CreateAccountResult::InvalidInput;
    return out;
  }

  out.result = CreateAccountResult::Ok;
  out.account_id = account_id;
  return out;
}

std::optional<std::string> UserStore::verify_login(const std::string& username, const std::string& password) {
  if (!db_ || username.empty() || password.empty()) {
    return std::nullopt;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT id, password_hash, salt FROM accounts WHERE username = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, username.c_str(), static_cast<int>(username.size()), SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) != SQLITE_ROW) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  const char* hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
  const char* salt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
  if (!id || !hash || !salt) {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }
  const bool ok = auth::verify_password(password, salt, hash);
  const std::string account_id = ok ? std::string(id) : std::string{};
  sqlite3_finalize(stmt);
  if (!ok) {
    return std::nullopt;
  }
  return account_id;
}

std::string UserStore::create_session(const std::string& account_id) {
  if (!db_ || account_id.empty()) {
    return {};
  }
  const std::string token = auth::generate_token();
  const auto now = std::chrono::system_clock::now().time_since_epoch().count();

  sqlite3_stmt* stmt = nullptr;
  const char* sql = "INSERT INTO sessions(token, account_id, created_at) VALUES (?1, ?2, ?3)";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return {};
  }
  sqlite3_bind_text(stmt, 1, token.c_str(), static_cast<int>(token.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, account_id.c_str(), static_cast<int>(account_id.size()), SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(now));
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE ? token : std::string{};
}

std::string UserStore::resolve_session(const std::string& session_token) const {
  if (!db_ || session_token.empty()) {
    return {};
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT account_id FROM sessions WHERE token = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return {};
  }
  sqlite3_bind_text(stmt, 1, session_token.c_str(), static_cast<int>(session_token.size()), SQLITE_TRANSIENT);
  std::string account_id;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (text) {
      account_id = text;
    }
  }
  sqlite3_finalize(stmt);
  return account_id;
}

std::optional<std::string> UserStore::username_for_account(const std::string& account_id) const {
  if (!db_ || account_id.empty()) {
    return std::nullopt;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "SELECT username FROM accounts WHERE id = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(stmt, 1, account_id.c_str(), static_cast<int>(account_id.size()), SQLITE_TRANSIENT);
  std::optional<std::string> username;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (text) {
      username = text;
    }
  }
  sqlite3_finalize(stmt);
  return username;
}

bool UserStore::delete_session(const std::string& session_token) {
  if (!db_ || session_token.empty()) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM sessions WHERE token = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, session_token.c_str(), static_cast<int>(session_token.size()), SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

void UserStore::merge_known_words(const std::string& from_id, const std::string& to_account_id) {
  if (!db_ || from_id.empty() || to_account_id.empty() || from_id == to_account_id) {
    return;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql =
      "INSERT OR IGNORE INTO known_words(token, word_id) "
      "SELECT ?1, word_id FROM known_words WHERE token = ?2";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return;
  }
  sqlite3_bind_text(stmt, 1, to_account_id.c_str(), static_cast<int>(to_account_id.size()), SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, from_id.c_str(), static_cast<int>(from_id.size()), SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

bool UserStore::clear_known_words(const std::string& account_id) {
  if (!db_ || account_id.empty()) {
    return false;
  }
  sqlite3_stmt* stmt = nullptr;
  const char* sql = "DELETE FROM known_words WHERE token = ?1";
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(stmt, 1, account_id.c_str(), static_cast<int>(account_id.size()), SQLITE_TRANSIENT);
  const int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}
