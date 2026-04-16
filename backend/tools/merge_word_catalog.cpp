// Merges JSON array chunk files into a base words.json (UTF-8, pretty-printed).
// Usage:
//   merge_word_catalog [--max-append N] [--expect-total M] <words.json> <chunk.json> [more chunks...]
//
// Each input must be a JSON array of objects with a string field "id".
// Duplicate ids (against the base file or across chunks) are an error.
// --max-append: after scanning chunks in order, keep only the first N newly collected entries
//   (same idea as concatenating all new rows then truncating).
// --expect-total: if set, the final array length must equal M or the program exits with an error.

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

void print_usage(const char* prog) {
  std::cerr << "Usage:\n  " << prog
            << " [--max-append N] [--expect-total M] <words.json> <chunk.json> [chunk.json ...]\n"
            << "\nMerges vocabulary chunks (JSON arrays) into words.json.\n"
            << "Duplicate \"id\" values vs the base file or between chunks are rejected.\n";
}

nlohmann::json read_json_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("cannot open for read: " + path);
  }
  nlohmann::json j;
  in >> j;
  return j;
}

void write_json_file(const std::string& path, const nlohmann::json& j) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    throw std::runtime_error("cannot open for write: " + path);
  }
  out << j.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
  out << "\n";
}

void require_array(const nlohmann::json& j, const char* ctx) {
  if (!j.is_array()) {
    throw std::runtime_error(std::string("expected JSON array in ") + ctx);
  }
}

void require_id_object(const nlohmann::json& el, const char* ctx) {
  if (!el.is_object() || !el.contains("id") || !el["id"].is_string()) {
    throw std::runtime_error(std::string("chunk entry missing string id in ") + ctx);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::size_t max_append = static_cast<std::size_t>(-1);
  bool has_max_append = false;
  std::size_t expect_total = 0;
  bool has_expect_total = false;
  std::vector<std::string> positional;

  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--max-append") {
      if (i + 1 >= argc) {
        std::cerr << "missing value for --max-append\n";
        return 2;
      }
      max_append = static_cast<std::size_t>(std::stoull(argv[++i]));
      has_max_append = true;
    } else if (a == "--expect-total") {
      if (i + 1 >= argc) {
        std::cerr << "missing value for --expect-total\n";
        return 2;
      }
      expect_total = static_cast<std::size_t>(std::stoull(argv[++i]));
      has_expect_total = true;
    } else if (a.find("--") == 0) {
      std::cerr << "unknown option: " << a << "\n";
      print_usage(argv[0]);
      return 2;
    } else {
      positional.push_back(a);
    }
  }

  if (positional.size() < 2) {
    print_usage(argv[0]);
    return 2;
  }

  const std::string& words_path = positional[0];

  try {
    nlohmann::json base = read_json_file(words_path);
    require_array(base, words_path.c_str());

    std::unordered_set<std::string> seen;
    for (const auto& el : base) {
      require_id_object(el, words_path.c_str());
      const std::string& id = el["id"].get_ref<const std::string&>();
      if (!seen.insert(id).second) {
        throw std::runtime_error("duplicate id in base file: " + id);
      }
    }

    nlohmann::json staged = nlohmann::json::array();

    for (std::size_t ci = 1; ci < positional.size(); ++ci) {
      const std::string& chunk_path = positional[ci];
      nlohmann::json chunk = read_json_file(chunk_path);
      require_array(chunk, chunk_path.c_str());

      for (const auto& el : chunk) {
        require_id_object(el, chunk_path.c_str());
        const std::string& id = el["id"].get_ref<const std::string&>();
        if (!seen.insert(id).second) {
          throw std::runtime_error("duplicate id (chunk vs base or earlier chunk): " + id);
        }
        staged.push_back(el);
      }
    }

    if (has_max_append && staged.size() > max_append) {
      staged.erase(staged.begin() + static_cast<nlohmann::json::difference_type>(max_append), staged.end());
    }

    for (const auto& el : staged) {
      base.push_back(el);
    }

    if (has_expect_total && base.size() != expect_total) {
      throw std::runtime_error("expected total " + std::to_string(expect_total) + " words, got " +
                               std::to_string(base.size()));
    }

    write_json_file(words_path, base);
    std::cout << "Wrote " << words_path << " (" << base.size() << " entries)\n";
  } catch (const std::exception& ex) {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
