#pragma once

#include <sqlite3.h>

#include <optional>
#include <string>
#include <unordered_set>

enum class CreateAccountResult { Ok, UsernameTaken, InvalidInput };

struct CreateAccountOutcome {
  CreateAccountResult result = CreateAccountResult::InvalidInput;
  std::string account_id;
};

class UserStore {
 public:
  explicit UserStore(std::string db_path);
  ~UserStore();

  UserStore(const UserStore&) = delete;
  UserStore& operator=(const UserStore&) = delete;

  bool open();

  void ensure_user(const std::string& token);

  bool add_known(const std::string& token, const std::string& word_id);

  std::unordered_set<std::string> known_ids(const std::string& token) const;

  CreateAccountOutcome create_account(const std::string& username, const std::string& password);

  std::optional<std::string> verify_login(const std::string& username, const std::string& password);

  std::string create_session(const std::string& account_id);

  std::string resolve_session(const std::string& session_token) const;

  std::optional<std::string> username_for_account(const std::string& account_id) const;

  bool delete_session(const std::string& session_token);

  void merge_known_words(const std::string& from_id, const std::string& to_account_id);

  bool clear_known_words(const std::string& account_id);

 private:
  std::string db_path_;
  sqlite3* db_ = nullptr;
};
