#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

struct WordEntry {
  std::string id;
  std::string hindi;
  std::string urdu;
  std::string roman;
  std::string pronunciation;
  std::string meaning_en;
  std::string etymology_en;
  std::string example_sentence;
  std::vector<std::string> synonyms_en;
};

class WordCatalog {
 public:
  bool load_from_file(const std::string& path);

  const std::vector<WordEntry>& words() const { return words_; }

  std::optional<WordEntry> find_by_id(const std::string& id) const;

  /// Deterministic pick for date; skips ids in `known`. Returns nullopt if all known.
  std::optional<WordEntry> word_of_day(const std::string& date_yyyy_mm_dd,
                                       const std::unordered_set<std::string>& known) const;

  std::optional<WordEntry> random_from_known(const std::unordered_set<std::string>& known_ids,
                                             uint64_t seed) const;

 private:
  std::vector<WordEntry> words_;
};

uint64_t hash_string_fnv1a64(const std::string& s);
