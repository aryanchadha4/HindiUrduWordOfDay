// Append vocabulary rows from a UTF-8 TSV into words.json (JSON array, UTF-8, indented).
//
// Usage:
//   append_catalog_tsv [--expect-rows N] <words.json> <catalog.tsv>
//
// TSV format (first row = header, tab-separated):
//   id	hindi	urdu	roman	pronunciation	meaning_en	etymology_en	example_sentence	synonyms_en
//
// synonyms_en is optional; use semicolons between English synonyms (no tabs inside fields).

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

std::string read_all_utf8(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open for read: " + path);
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void write_json_file(const std::string& path, const nlohmann::json& j) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("cannot open for write: " + path);
  }
  out << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
  out << "\n";
}

std::string trim(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
    s.pop_back();
  }
  return s;
}

std::vector<std::string> split_tabs(const std::string& line) {
  std::vector<std::string> cols;
  std::string cur;
  for (unsigned char uc : line) {
    const char c = static_cast<char>(uc);
    if (c == '\t') {
      cols.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  cols.push_back(cur);
  return cols;
}

std::vector<std::string> split_semicolons(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (unsigned char uc : s) {
    const char c = static_cast<char>(uc);
    if (c == ';') {
      const std::string t = trim(cur);
      if (!t.empty()) {
        out.push_back(t);
      }
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  const std::string t = trim(cur);
  if (!t.empty()) {
    out.push_back(t);
  }
  return out;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::string cur;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\n') {
      if (!cur.empty() && cur.back() == '\r') {
        cur.pop_back();
      }
      lines.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  if (!cur.empty()) {
    if (!cur.empty() && cur.back() == '\r') {
      cur.pop_back();
    }
    lines.push_back(cur);
  }
  return lines;
}

void expect_header(const std::vector<std::string>& cols) {
  static const char* kExpected[] = {"id",         "hindi",        "urdu",           "roman",
                                    "pronunciation", "meaning_en", "etymology_en", "example_sentence",
                                    "synonyms_en"};
  if (cols.size() != 9) {
    throw std::runtime_error("header row must have 9 tab-separated columns");
  }
  for (size_t i = 0; i < 9; ++i) {
    if (trim(cols[i]) != kExpected[i]) {
      throw std::runtime_error(std::string("unexpected header column ") + std::to_string(i) + ": got '" +
                               trim(cols[i]) + "', expected '" + kExpected[i] + "'");
    }
  }
}

nlohmann::json row_to_entry(const std::vector<std::string>& cols, size_t line_no) {
  if (cols.size() != 9) {
    throw std::runtime_error("line " + std::to_string(line_no) + ": expected 9 columns, got " +
                             std::to_string(cols.size()));
  }
  nlohmann::json o;
  o["id"] = trim(cols[0]);
  o["hindi"] = trim(cols[1]);
  o["urdu"] = trim(cols[2]);
  o["roman"] = trim(cols[3]);
  o["pronunciation"] = trim(cols[4]);
  o["meaning_en"] = trim(cols[5]);
  o["etymology_en"] = trim(cols[6]);
  o["example_sentence"] = trim(cols[7]);
  o["synonyms_en"] = nlohmann::json::array();
  for (const auto& s : split_semicolons(cols[8])) {
    o["synonyms_en"].push_back(s);
  }
  if (!o["id"].is_string() || o["id"].get<std::string>().empty()) {
    throw std::runtime_error("line " + std::to_string(line_no) + ": missing id");
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t expect_rows = 0;
  bool has_expect_rows = false;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--expect-rows") {
      if (i + 1 >= argc) {
        std::cerr << "missing value for --expect-rows\n";
        return 2;
      }
      expect_rows = static_cast<std::size_t>(std::stoull(argv[++i]));
      has_expect_rows = true;
    } else if (a.find("--") == 0) {
      std::cerr << "unknown option: " << a << "\n";
      std::cerr << "Usage: append_catalog_tsv [--expect-rows N] <words.json> <catalog.tsv>\n";
      return 2;
    } else {
      positional.push_back(a);
    }
  }

  if (positional.size() != 2) {
    std::cerr << "Usage: append_catalog_tsv [--expect-rows N] <words.json> <catalog.tsv>\n";
    return 2;
  }

  const std::string& words_path = positional[0];
  const std::string& tsv_path = positional[1];

  try {
    nlohmann::json base = nlohmann::json::parse(read_all_utf8(words_path));
    if (!base.is_array()) {
      throw std::runtime_error("words.json must be a JSON array");
    }

    std::unordered_set<std::string> seen;
    for (const auto& el : base) {
      if (!el.is_object() || !el.contains("id") || !el["id"].is_string()) {
        throw std::runtime_error("words.json entry missing string id");
      }
      const std::string id = el["id"].get<std::string>();
      if (!seen.insert(id).second) {
        throw std::runtime_error("duplicate id in words.json: " + id);
      }
    }

    const std::string tsv_text = read_all_utf8(tsv_path);
    const std::vector<std::string> lines = split_lines(tsv_text);
    if (lines.empty()) {
      throw std::runtime_error("empty TSV");
    }

    std::vector<std::string> header = split_tabs(lines[0]);
    expect_header(header);

    std::vector<nlohmann::json> additions;
    for (size_t li = 1; li < lines.size(); ++li) {
      const std::string raw = trim(lines[li]);
      if (raw.empty()) {
        continue;
      }
      const std::vector<std::string> cols = split_tabs(raw);
      nlohmann::json entry = row_to_entry(cols, li + 1);
      const std::string id = entry["id"].get<std::string>();
      if (!seen.insert(id).second) {
        throw std::runtime_error("duplicate id (TSV vs words.json or within TSV): " + id);
      }
      additions.push_back(std::move(entry));
    }

    if (has_expect_rows && additions.size() != expect_rows) {
      throw std::runtime_error("expected " + std::to_string(expect_rows) + " TSV data rows, got " +
                               std::to_string(additions.size()));
    }

    for (auto& e : additions) {
      base.push_back(std::move(e));
    }

    write_json_file(words_path, base);
    std::cout << "Appended " << additions.size() << " entries; total " << base.size() << " -> " << words_path
              << "\n";
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
