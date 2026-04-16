#pragma once

#include <string>
#include <vector>

namespace fuzzy {

std::string normalize(const std::string& s);

/// Ratio in [0,1]: 1 = identical after normalization.
double similarity_ratio(const std::string& a, const std::string& b);

struct GradeResult {
  bool correct = false;
  double similarity = 0.0;
  std::string reference_used;
};

/// Accepts if best similarity across references >= threshold.
GradeResult grade_meaning(const std::string& user_answer,
                          const std::string& primary_meaning,
                          const std::vector<std::string>& extra_synonyms,
                          double threshold = 0.82);

}  // namespace fuzzy
