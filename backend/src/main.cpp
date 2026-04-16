#include "catalog.hpp"
#include "db.hpp"
#include "fuzzy.hpp"

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <random>
#include <sstream>

namespace {

std::string today_utc_date() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[16];
  std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
  return std::string(buf);
}

std::string get_token(const httplib::Request& req) {
  if (req.has_header("X-User-Token")) {
    return req.get_header_value("X-User-Token");
  }
  return {};
}

void apply_cors(httplib::Response& res) {
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Headers", "Content-Type, X-User-Token");
  res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}

nlohmann::json word_to_json_full(const WordEntry& w) {
  nlohmann::json j;
  j["id"] = w.id;
  j["hindi"] = w.hindi;
  j["urdu"] = w.urdu;
  j["roman"] = w.roman;
  j["pronunciation"] = w.pronunciation;
  j["meaning_en"] = w.meaning_en;
  j["etymology_en"] = w.etymology_en;
  j["example_sentence"] = w.example_sentence;
  j["synonyms_en"] = w.synonyms_en;
  return j;
}

nlohmann::json word_to_json_quiz_prompt(const WordEntry& w) {
  nlohmann::json j;
  j["word_id"] = w.id;
  j["hindi"] = w.hindi;
  j["urdu"] = w.urdu;
  j["roman"] = w.roman;
  j["pronunciation"] = w.pronunciation;
  return j;
}

}  // namespace

int main(int argc, char** argv) {
  std::string words_path = "./data/words.json";
  std::string db_path = "./data/users.db";
  int port = 8080;
  if (const char* env_words = std::getenv("WORDS_JSON")) {
    words_path = env_words;
  }
  if (const char* env_db = std::getenv("USER_DB")) {
    db_path = env_db;
  }
  if (const char* env_port = std::getenv("PORT")) {
    port = std::atoi(env_port);
  }
  if (argc >= 2) {
    words_path = argv[1];
  }
  if (argc >= 3) {
    db_path = argv[2];
  }
  if (argc >= 4) {
    port = std::atoi(argv[3]);
  }

  WordCatalog catalog;
  if (!catalog.load_from_file(words_path)) {
    std::cerr << "Failed to load words from: " << words_path << "\n";
    return 1;
  }

  UserStore store(db_path);
  if (!store.open()) {
    std::cerr << "Failed to open database: " << db_path << "\n";
    return 1;
  }

  httplib::Server svr;

  svr.set_pre_routing_handler([&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    if (req.method == "OPTIONS") {
      res.status = 204;
      return httplib::Server::HandlerResponse::Handled;
    }
    return httplib::Server::HandlerResponse::Unhandled;
  });

  svr.Get("/api/health", [&](const httplib::Request&, httplib::Response& res) {
    apply_cors(res);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Get("/api/word-of-day", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    std::string date = req.get_param_value("date");
    if (date.empty()) {
      date = today_utc_date();
    }
    const std::string token = get_token(req);
    std::unordered_set<std::string> known;
    if (!token.empty()) {
      known = store.known_ids(token);
    }
    const auto w = catalog.word_of_day(date, known);
    nlohmann::json body;
    body["known_count"] = known.size();
    if (!w) {
      body["word"] = nullptr;
      body["message"] = "No eligible words (all marked known).";
    } else {
      body["word"] = word_to_json_full(*w);
    }
    res.set_content(body.dump(), "application/json");
  });

  svr.Post("/api/known", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    const std::string token = get_token(req);
    if (token.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"missing X-User-Token"})", "application/json");
      return;
    }
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid JSON"})", "application/json");
      return;
    }
    if (!body.contains("word_id") || !body["word_id"].is_string()) {
      res.status = 400;
      res.set_content(R"({"error":"word_id required"})", "application/json");
      return;
    }
    const std::string word_id = body["word_id"].get<std::string>();
    if (!catalog.find_by_id(word_id)) {
      res.status = 404;
      res.set_content(R"({"error":"unknown word_id"})", "application/json");
      return;
    }
    store.add_known(token, word_id);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Get("/api/quiz", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    const std::string token = get_token(req);
    if (token.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"missing X-User-Token"})", "application/json");
      return;
    }
    const auto known = store.known_ids(token);
    std::random_device rd;
    const uint64_t seed =
        (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd()) ^ hash_string_fnv1a64(token);
    const auto w = catalog.random_from_known(known, seed);
    nlohmann::json out;
    if (!w) {
      out["prompt"] = nullptr;
      out["message"] = "Mark at least one word as known to start a quiz.";
      res.set_content(out.dump(), "application/json");
      return;
    }
    out["prompt"] = word_to_json_quiz_prompt(*w);
    res.set_content(out.dump(), "application/json");
  });

  svr.Post("/api/quiz/check", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    const std::string token = get_token(req);
    if (token.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"missing X-User-Token"})", "application/json");
      return;
    }
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid JSON"})", "application/json");
      return;
    }
    if (!body.contains("word_id") || !body["word_id"].is_string() || !body.contains("answer") ||
        !body["answer"].is_string()) {
      res.status = 400;
      res.set_content(R"({"error":"word_id and answer required"})", "application/json");
      return;
    }
    const std::string word_id = body["word_id"].get<std::string>();
    const std::string answer = body["answer"].get<std::string>();
    const auto known = store.known_ids(token);
    if (known.find(word_id) == known.end()) {
      res.status = 403;
      res.set_content(R"({"error":"word not in your known set"})", "application/json");
      return;
    }
    const auto w = catalog.find_by_id(word_id);
    if (!w) {
      res.status = 404;
      res.set_content(R"({"error":"unknown word"})", "application/json");
      return;
    }
    const auto grade = fuzzy::grade_meaning(answer, w->meaning_en, w->synonyms_en, 0.82);
    nlohmann::json out;
    out["correct"] = grade.correct;
    out["similarity"] = grade.similarity;
    if (!grade.correct) {
      out["expected_hint"] = w->meaning_en;
    }
    res.set_content(out.dump(), "application/json");
  });

  std::cout << "Listening on http://127.0.0.1:" << port << " words=" << words_path << " db=" << db_path
            << "\n";
  if (!svr.listen("0.0.0.0", port)) {
    std::cerr << "Failed to bind port " << port << "\n";
    return 1;
  }
  return 0;
}
