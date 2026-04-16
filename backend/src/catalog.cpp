#include "catalog.hpp"

#include <fstream>
#include <random>
#include <sstream>

uint64_t hash_string_fnv1a64(const std::string& s) {
  constexpr uint64_t offset = 14695981039346656037ULL;
  constexpr uint64_t prime = 1099511628211ULL;
  uint64_t h = offset;
  for (unsigned char c : s) {
    h ^= static_cast<uint64_t>(c);
    h *= prime;
  }
  return h;
}

bool WordCatalog::load_from_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    return false;
  }
  nlohmann::json j;
  in >> j;
  if (!j.is_array()) {
    return false;
  }
  words_.clear();
  for (const auto& item : j) {
    WordEntry w;
    w.id = item.value("id", "");
    w.hindi = item.value("hindi", "");
    w.urdu = item.value("urdu", "");
    w.roman = item.value("roman", "");
    w.pronunciation = item.value("pronunciation", "");
    w.meaning_en = item.value("meaning_en", "");
    w.etymology_en = item.value("etymology_en", "");
    w.example_sentence = item.value("example_sentence", "");
    if (item.contains("synonyms_en") && item["synonyms_en"].is_array()) {
      for (const auto& syn : item["synonyms_en"]) {
        if (syn.is_string()) {
          w.synonyms_en.push_back(syn.get<std::string>());
        }
      }
    }
    if (!w.id.empty() && !w.hindi.empty()) {
      words_.push_back(std::move(w));
    }
  }
  return !words_.empty();
}

std::optional<WordEntry> WordCatalog::find_by_id(const std::string& id) const {
  for (const auto& w : words_) {
    if (w.id == id) {
      return w;
    }
  }
  return std::nullopt;
}

std::optional<WordEntry> WordCatalog::word_of_day(const std::string& date_yyyy_mm_dd,
                                                  const std::unordered_set<std::string>& known) const {
  if (words_.empty()) {
    return std::nullopt;
  }
  const uint64_t h = hash_string_fnv1a64(date_yyyy_mm_dd + "|hindiurdu_wotd_v1");
  size_t start = static_cast<size_t>(h % words_.size());
  for (size_t k = 0; k < words_.size(); ++k) {
    const size_t idx = (start + k) % words_.size();
    const auto& w = words_[idx];
    if (known.find(w.id) == known.end()) {
      return w;
    }
  }
  return std::nullopt;
}

std::optional<WordEntry> WordCatalog::random_from_known(const std::unordered_set<std::string>& known_ids,
                                                        uint64_t seed) const {
  std::vector<const WordEntry*> pool;
  pool.reserve(known_ids.size());
  for (const auto& w : words_) {
    if (known_ids.find(w.id) != known_ids.end()) {
      pool.push_back(&w);
    }
  }
  if (pool.empty()) {
    return std::nullopt;
  }
  std::mt19937_64 rng(seed);
  std::uniform_int_distribution<size_t> dist(0, pool.size() - 1);
  return *pool[dist(rng)];
}
