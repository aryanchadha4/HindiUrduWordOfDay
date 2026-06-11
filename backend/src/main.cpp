#include "auth.hpp"
#include "catalog.hpp"
#include "db.hpp"
#include "fuzzy.hpp"

#include <httplib.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

std::string get_session_token(const httplib::Request& req) {
  if (req.has_header("X-User-Token")) {
    return req.get_header_value("X-User-Token");
  }
  if (req.has_header("Cookie")) {
    const std::string cookies = req.get_header_value("Cookie");
    constexpr const char* prefix = "session=";
    const auto pos = cookies.find(prefix);
    if (pos != std::string::npos) {
      const auto start = pos + std::strlen(prefix);
      auto end = cookies.find(';', start);
      if (end == std::string::npos) {
        end = cookies.size();
      }
      return cookies.substr(start, end - start);
    }
  }
  return {};
}

std::string get_anon_token(const httplib::Request& req) {
  if (req.has_header("X-Anon-Token")) {
    return req.get_header_value("X-Anon-Token");
  }
  return {};
}

std::string resolve_known_user_id(const httplib::Request& req, const UserStore& store) {
  const std::string session = get_session_token(req);
  if (!session.empty()) {
    const std::string account_id = store.resolve_session(session);
    if (!account_id.empty()) {
      return account_id;
    }
    return session;
  }
  return {};
}

void set_session_cookie(httplib::Response& res, const std::string& token) {
  res.set_header("Set-Cookie",
                 "session=" + token + "; HttpOnly; Secure; SameSite=Lax; Max-Age=31536000; Path=/");
}

void clear_session_cookie(httplib::Response& res) {
  res.set_header("Set-Cookie", "session=; HttpOnly; Secure; SameSite=Lax; Max-Age=0; Path=/");
}

void apply_cors(httplib::Response& res) {
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Headers", "Content-Type, X-User-Token, X-Anon-Token");
  res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
}

std::string normalize_username(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (unsigned char c : raw) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

void merge_anon_progress(const httplib::Request& req, UserStore& store, const std::string& account_id,
                         const nlohmann::json& body) {
  std::string anon = get_anon_token(req);
  if (anon.empty() && body.contains("anon_token") && body["anon_token"].is_string()) {
    anon = body["anon_token"].get<std::string>();
  }
  if (!anon.empty() && anon != account_id) {
    store.merge_known_words(anon, account_id);
  }
}

nlohmann::json auth_success_body(const std::string& token, const std::string& username) {
  nlohmann::json out;
  out["token"] = token;
  out["username"] = username;
  return out;
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

std::string content_type_for(const std::filesystem::path& path) {
  const auto ext = path.extension().string();
  if (ext == ".html") {
    return "text/html; charset=utf-8";
  }
  if (ext == ".js") {
    return "application/javascript; charset=utf-8";
  }
  if (ext == ".css") {
    return "text/css; charset=utf-8";
  }
  if (ext == ".json") {
    return "application/json; charset=utf-8";
  }
  if (ext == ".svg") {
    return "image/svg+xml";
  }
  if (ext == ".png") {
    return "image/png";
  }
  if (ext == ".ico") {
    return "image/x-icon";
  }
  return "application/octet-stream";
}

bool serve_static_file(const std::filesystem::path& base, const std::string& rel, httplib::Response& res) {
  const auto path = base / rel;
  if (!std::filesystem::is_regular_file(path)) {
    return false;
  }
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  res.set_content(ss.str(), content_type_for(path));
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::string words_path = "./data/words.json";
  std::string db_path = "./data/users.db";
  std::string static_dir;
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
  if (const char* env_static = std::getenv("STATIC_DIR")) {
    static_dir = env_static;
  }
  if (static_dir.empty()) {
    static_dir = "/app/frontend/dist";
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
  std::cout << "Loaded " << catalog.words().size() << " words from " << words_path << "\n";

  UserStore store(db_path);
  if (!store.open()) {
    std::cerr << "Failed to open database: " << db_path << "\n";
    return 1;
  }
  std::cout << "Opened user database: " << db_path << "\n";

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

  svr.Get("/api/auth/me", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    const std::string session = get_session_token(req);
    if (session.empty()) {
      res.status = 401;
      res.set_content(R"({"error":"not signed in"})", "application/json");
      return;
    }
    const std::string account_id = store.resolve_session(session);
    if (account_id.empty()) {
      res.status = 401;
      res.set_content(R"({"error":"invalid session"})", "application/json");
      return;
    }
    const auto username = store.username_for_account(account_id);
    if (!username) {
      res.status = 401;
      res.set_content(R"({"error":"invalid session"})", "application/json");
      return;
    }
    nlohmann::json out;
    out["username"] = *username;
    res.set_content(out.dump(), "application/json");
  });

  svr.Post("/api/auth/signup", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid JSON"})", "application/json");
      return;
    }
    if (!body.contains("username") || !body["username"].is_string() || !body.contains("password") ||
        !body["password"].is_string()) {
      res.status = 400;
      res.set_content(R"({"error":"username and password required"})", "application/json");
      return;
    }
    const std::string username = normalize_username(body["username"].get<std::string>());
    const std::string password = body["password"].get<std::string>();
    const auto created = store.create_account(username, password);
    if (created.result == CreateAccountResult::UsernameTaken) {
      res.status = 409;
      res.set_content(R"({"error":"username already taken"})", "application/json");
      return;
    }
    if (created.result != CreateAccountResult::Ok) {
      res.status = 400;
      res.set_content(R"({"error":"invalid username or password"})", "application/json");
      return;
    }
    merge_anon_progress(req, store, created.account_id, body);
    const std::string token = store.create_session(created.account_id);
    if (token.empty()) {
      res.status = 500;
      res.set_content(R"({"error":"could not create session"})", "application/json");
      return;
    }
    set_session_cookie(res, token);
    res.set_content(auth_success_body(token, username).dump(), "application/json");
  });

  svr.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    nlohmann::json body;
    try {
      body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
    } catch (...) {
      res.status = 400;
      res.set_content(R"({"error":"invalid JSON"})", "application/json");
      return;
    }
    if (!body.contains("username") || !body["username"].is_string() || !body.contains("password") ||
        !body["password"].is_string()) {
      res.status = 400;
      res.set_content(R"({"error":"username and password required"})", "application/json");
      return;
    }
    const std::string username = normalize_username(body["username"].get<std::string>());
    const std::string password = body["password"].get<std::string>();
    const auto account_id = store.verify_login(username, password);
    if (!account_id) {
      res.status = 401;
      res.set_content(R"({"error":"invalid username or password"})", "application/json");
      return;
    }
    merge_anon_progress(req, store, *account_id, body);
    const std::string token = store.create_session(*account_id);
    if (token.empty()) {
      res.status = 500;
      res.set_content(R"({"error":"could not create session"})", "application/json");
      return;
    }
    set_session_cookie(res, token);
    res.set_content(auth_success_body(token, username).dump(), "application/json");
  });

  svr.Post("/api/auth/logout", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    const std::string session = get_session_token(req);
    if (!session.empty()) {
      store.delete_session(session);
    }
    clear_session_cookie(res);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Post("/api/auth/clear-progress", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    const std::string session = get_session_token(req);
    const std::string account_id = store.resolve_session(session);
    if (account_id.empty()) {
      res.status = 401;
      res.set_content(R"({"error":"not signed in"})", "application/json");
      return;
    }
    store.clear_known_words(account_id);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Get("/api/word-of-day", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    std::string date = req.get_param_value("date");
    if (date.empty()) {
      date = today_utc_date();
    }
    const std::string user_id = resolve_known_user_id(req, store);
    std::unordered_set<std::string> known;
    if (!user_id.empty()) {
      known = store.known_ids(user_id);
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
    const std::string user_id = resolve_known_user_id(req, store);
    if (user_id.empty()) {
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
    store.add_known(user_id, word_id);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Get("/api/quiz", [&](const httplib::Request& req, httplib::Response& res) {
    apply_cors(res);
    const std::string user_id = resolve_known_user_id(req, store);
    if (user_id.empty()) {
      res.status = 400;
      res.set_content(R"({"error":"missing X-User-Token"})", "application/json");
      return;
    }
    const auto known = store.known_ids(user_id);
    std::random_device rd;
    const uint64_t seed =
        (static_cast<uint64_t>(rd()) << 32) ^ static_cast<uint64_t>(rd()) ^ hash_string_fnv1a64(user_id);
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
    const std::string user_id = resolve_known_user_id(req, store);
    if (user_id.empty()) {
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
    const auto known = store.known_ids(user_id);
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

  const std::filesystem::path static_path(static_dir);
  if (!std::filesystem::is_directory(static_path)) {
    std::cerr << "Warning: static dir not found: " << static_path << "\n";
  } else {
    const auto index_path = static_path / "index.html";
    if (!std::filesystem::is_regular_file(index_path)) {
      std::cerr << "Warning: index.html missing in static dir: " << static_path << "\n";
    } else {
      const auto serve_index = [static_path](const httplib::Request&, httplib::Response& res) {
        if (!serve_static_file(static_path, "index.html", res)) {
          res.status = 404;
          res.set_content("index.html not found", "text/plain; charset=utf-8");
        }
      };
      svr.Get("/", serve_index);
      svr.Get("/index.html", serve_index);

      const auto assets_path = static_path / "assets";
      if (std::filesystem::is_directory(assets_path)) {
        if (!svr.set_mount_point("/assets", assets_path.string())) {
          std::cerr << "Warning: failed to mount assets dir: " << assets_path << "\n";
        }
      }
      std::cout << "Serving static files from " << static_path << "\n";
    }
  }

  std::cout << "Listening on http://127.0.0.1:" << port << " words=" << words_path << " db=" << db_path
            << "\n";
  if (!svr.listen("0.0.0.0", port)) {
    std::cerr << "Failed to bind port " << port << "\n";
    return 1;
  }
  return 0;
}
