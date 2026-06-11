#include "auth.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cctype>
#include <iomanip>
#include <random>
#include <sstream>

namespace auth {

namespace {

constexpr int kSaltBytes = 16;
constexpr int kHashBytes = 32;
constexpr int kPbkdf2Iterations = 100000;

std::string bytes_to_hex(const unsigned char* data, size_t len) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < len; ++i) {
    ss << std::setw(2) << static_cast<unsigned>(data[i]);
  }
  return ss.str();
}

bool hex_to_bytes(const std::string& hex, std::array<unsigned char, kSaltBytes>& out) {
  if (hex.size() != kSaltBytes * 2) {
    return false;
  }
  for (size_t i = 0; i < kSaltBytes; ++i) {
    const auto byte = hex.substr(i * 2, 2);
    try {
      out[i] = static_cast<unsigned char>(std::stoul(byte, nullptr, 16));
    } catch (...) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool is_valid_username(const std::string& username) {
  if (username.size() < 3 || username.size() > 24) {
    return false;
  }
  for (unsigned char c : username) {
    if (!std::islower(c) && !std::isdigit(c) && c != '_') {
      return false;
    }
  }
  return true;
}

bool is_valid_password(const std::string& password) {
  return password.size() >= 6;
}

std::string generate_salt_hex() {
  std::array<unsigned char, kSaltBytes> salt{};
  if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : salt) {
      b = static_cast<unsigned char>(dist(gen));
    }
  }
  return bytes_to_hex(salt.data(), salt.size());
}

std::string hash_password(const std::string& password, const std::string& salt_hex) {
  std::array<unsigned char, kSaltBytes> salt{};
  if (!hex_to_bytes(salt_hex, salt)) {
    return {};
  }
  std::array<unsigned char, kHashBytes> hash{};
  if (PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()), salt.data(),
                        static_cast<int>(salt.size()), kPbkdf2Iterations, EVP_sha256(),
                        static_cast<int>(hash.size()), hash.data()) != 1) {
    return {};
  }
  return bytes_to_hex(hash.data(), hash.size());
}

bool verify_password(const std::string& password, const std::string& salt_hex, const std::string& hash_hex) {
  const std::string computed = hash_password(password, salt_hex);
  return !computed.empty() && computed == hash_hex;
}

std::string generate_token() {
  std::array<unsigned char, 16> bytes{};
  if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : bytes) {
      b = static_cast<unsigned char>(dist(gen));
    }
  }
  bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      ss << '-';
    }
    ss << std::setw(2) << static_cast<unsigned>(bytes[i]);
  }
  return ss.str();
}

}  // namespace auth
