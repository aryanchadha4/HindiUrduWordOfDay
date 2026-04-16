#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "fuzzy.hpp"

TEST_CASE("normalize trims and lowercases", "[fuzzy]") {
  REQUIRE(fuzzy::normalize("  Hello,  World!  ") == "hello world");
}

TEST_CASE("similarity exact", "[fuzzy]") {
  REQUIRE_THAT(fuzzy::similarity_ratio("courage", "courage"),
               Catch::Matchers::WithinAbs(1.0, 1e-9));
}

TEST_CASE("grade accepts close typos", "[fuzzy]") {
  const auto g = fuzzy::grade_meaning("peacful state of mind", "peaceful state of mind", {}, 0.82);
  REQUIRE(g.correct);
  REQUIRE(g.similarity > 0.82);
}

TEST_CASE("grade uses synonyms", "[fuzzy]") {
  const auto g =
      fuzzy::grade_meaning("courage", "bravery", std::vector<std::string>{"courage", "valor"}, 0.82);
  REQUIRE(g.correct);
}

TEST_CASE("grade rejects unrelated", "[fuzzy]") {
  const auto g = fuzzy::grade_meaning("completely wrong", "peaceful state of mind", {}, 0.82);
  REQUIRE_FALSE(g.correct);
}

TEST_CASE("grade accepts one clause of or gloss", "[fuzzy]") {
  const auto g =
      fuzzy::grade_meaning("luster", "luster or festive bustle", std::vector<std::string>{}, 0.82);
  REQUIRE(g.correct);
  REQUIRE(g.similarity > 0.82);
}

TEST_CASE("grade accepts phrase contained in gloss", "[fuzzy]") {
  const auto g = fuzzy::grade_meaning("festive bustle", "luster or festive bustle", {}, 0.82);
  REQUIRE(g.correct);
}
