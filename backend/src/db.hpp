#pragma once

#include <sqlite3.h>

#include <string>
#include <unordered_set>
#include <vector>

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

 private:
  std::string db_path_;
  sqlite3* db_ = nullptr;
};
