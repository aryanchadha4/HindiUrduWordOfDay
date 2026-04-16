#include "fuzzy.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace fuzzy {

namespace {

bool is_word_char(unsigned char c) {
  return std::isalnum(c) != 0;
}

void trim_inplace(std::string& s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.pop_back();
  }
}

bool starts_with_ci(std::string_view hay, std::string_view needle) {
  if (needle.size() > hay.size()) {
    return false;
  }
  for (size_t i = 0; i < needle.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(hay[i])) !=
        std::tolower(static_cast<unsigned char>(needle[i]))) {
      return false;
    }
  }
  return true;
}

size_t find_ci_substr(const std::string& hay, std::string_view needle_lower) {
  if (needle_lower.empty() || hay.size() < needle_lower.size()) {
    return std::string::npos;
  }
  for (size_t i = 0; i + needle_lower.size() <= hay.size(); ++i) {
    bool ok = true;
    for (size_t j = 0; j < needle_lower.size(); ++j) {
      if (std::tolower(static_cast<unsigned char>(hay[i + j])) != needle_lower[j]) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return i;
    }
  }
  return std::string::npos;
}

std::vector<std::string> split_on_chars(const std::string& s, const char* seps) {
  std::vector<std::string> out;
  std::string cur;
  for (unsigned char uc : s) {
    const char c = static_cast<char>(uc);
    bool is_sep = false;
    for (const char* p = seps; *p; ++p) {
      if (c == *p) {
        is_sep = true;
        break;
      }
    }
    if (is_sep) {
      trim_inplace(cur);
      if (!cur.empty()) {
        out.push_back(cur);
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  trim_inplace(cur);
  if (!cur.empty()) {
    out.push_back(cur);
  }
  return out;
}

std::vector<std::string> split_or_and_recursive(std::string piece) {
  trim_inplace(piece);
  if (piece.empty()) {
    return {};
  }
  static constexpr std::string_view kOr = " or ";
  static constexpr std::string_view kAnd = " and ";
  size_t best = std::string::npos;
  size_t best_len = 0;
  const size_t p_or = find_ci_substr(piece, kOr);
  if (p_or != std::string::npos) {
    best = p_or;
    best_len = kOr.size();
  }
  const size_t p_and = find_ci_substr(piece, kAnd);
  if (p_and != std::string::npos && (best == std::string::npos || p_and < best)) {
    best = p_and;
    best_len = kAnd.size();
  }
  if (best == std::string::npos) {
    return {piece};
  }
  std::string left = piece.substr(0, best);
  std::string right = piece.substr(best + best_len);
  trim_inplace(left);
  trim_inplace(right);
  std::vector<std::string> out;
  for (const auto& a : split_or_and_recursive(left)) {
    out.push_back(a);
  }
  for (const auto& b : split_or_and_recursive(right)) {
    out.push_back(b);
  }
  return out;
}

std::vector<std::string> expand_meaning_candidates(const std::string& ref) {
  std::vector<std::string> acc;
  auto push_unique = [&acc](const std::string& v) {
    std::string t = v;
    trim_inplace(t);
    if (t.empty()) {
      return;
    }
    for (const auto& e : acc) {
      if (e == t) {
        return;
      }
    }
    acc.push_back(t);
  };

  push_unique(ref);
  for (const auto& comma_part : split_on_chars(ref, ",;/")) {
    push_unique(comma_part);
    for (const auto& clause : split_or_and_recursive(comma_part)) {
      push_unique(clause);
    }
  }
  return acc;
}

/// Whole-word / phrase match of user inside reference gloss (normalized).
double phrase_containment_score(std::string_view user_n, std::string_view ref_n) {
  if (user_n.empty() || ref_n.empty()) {
    return 0.0;
  }
  if (user_n == ref_n) {
    return 1.0;
  }
  if (user_n.size() > ref_n.size()) {
    return 0.0;
  }
  for (size_t i = 0; i + user_n.size() <= ref_n.size(); ++i) {
    if (!starts_with_ci(ref_n.substr(i), user_n)) {
      continue;
    }
    const size_t end = i + user_n.size();
    const bool left_ok = (i == 0) || ref_n[i - 1] == ' ';
    const bool right_ok = (end >= ref_n.size()) || ref_n[end] == ' ';
    if (left_ok && right_ok) {
      return 0.95;
    }
  }
  return 0.0;
}

}  // namespace

std::string normalize(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  bool last_space = false;
  for (unsigned char uc : s) {
    char c = static_cast<char>(std::tolower(uc));
    if (std::isspace(uc) != 0) {
      if (!out.empty() && !last_space) {
        out.push_back(' ');
        last_space = true;
      }
      continue;
    }
    if (is_word_char(uc)) {
      out.push_back(c);
      last_space = false;
    }
    // drop punctuation
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

static size_t levenshtein(const std::string& a, const std::string& b) {
  const size_t n = a.size();
  const size_t m = b.size();
  if (n == 0) {
    return m;
  }
  if (m == 0) {
    return n;
  }
  std::vector<size_t> prev(m + 1), cur(m + 1);
  for (size_t j = 0; j <= m; ++j) {
    prev[j] = j;
  }
  for (size_t i = 1; i <= n; ++i) {
    cur[0] = i;
    for (size_t j = 1; j <= m; ++j) {
      size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
    }
    prev.swap(cur);
  }
  return prev[m];
}

double similarity_ratio(const std::string& a_raw, const std::string& b_raw) {
  const std::string a = normalize(a_raw);
  const std::string b = normalize(b_raw);
  if (a.empty() && b.empty()) {
    return 1.0;
  }
  if (a.empty() || b.empty()) {
    return 0.0;
  }
  const size_t dist = levenshtein(a, b);
  const size_t max_len = std::max(a.size(), b.size());
  return 1.0 - static_cast<double>(dist) / static_cast<double>(max_len);
}

GradeResult grade_meaning(const std::string& user_answer,
                          const std::string& primary_meaning,
                          const std::vector<std::string>& extra_synonyms,
                          double threshold) {
  GradeResult r;
  const std::string u_norm = normalize(user_answer);

  std::vector<std::string> base_refs;
  base_refs.push_back(primary_meaning);
  for (const auto& s : extra_synonyms) {
    if (!s.empty()) {
      base_refs.push_back(s);
    }
  }

  double best = 0.0;
  std::string best_ref = primary_meaning;

  for (const auto& base : base_refs) {
    if (base.empty()) {
      continue;
    }
    const std::string ref_norm = normalize(base);
    double local = similarity_ratio(user_answer, base);
    local = std::max(local, phrase_containment_score(std::string_view(u_norm), std::string_view(ref_norm)));

    for (const auto& variant : expand_meaning_candidates(base)) {
      const std::string v_norm = normalize(variant);
      local = std::max(local, similarity_ratio(user_answer, variant));
      local = std::max(local, phrase_containment_score(std::string_view(u_norm), std::string_view(v_norm)));
    }

    if (local > best) {
      best = local;
      best_ref = base;
    }
  }

  r.similarity = best;
  r.reference_used = best_ref;
  r.correct = best >= threshold;
  return r;
}

}  // namespace fuzzy
