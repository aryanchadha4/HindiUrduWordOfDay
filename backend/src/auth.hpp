#pragma once

#include <string>

namespace auth {

bool is_valid_username(const std::string& username);
bool is_valid_password(const std::string& password);

std::string generate_salt_hex();
std::string hash_password(const std::string& password, const std::string& salt_hex);
bool verify_password(const std::string& password, const std::string& salt_hex, const std::string& hash_hex);

std::string generate_token();

}  // namespace auth
