#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef OMNIDETECT_USE_GTEST
#include <gtest/gtest.h>
#define OMNI_TEST(name) TEST(OmniDetectCore, name)
#define OMNI_REQUIRE(expression) ASSERT_TRUE(expression)
#define OMNI_REQUIRE_NEAR(actual, expected, tolerance) ASSERT_NEAR((actual), (expected), (tolerance))
#else

namespace omnidetect::test {

using TestFunction = std::function<void()>;

struct TestCase {
  std::string name;
  TestFunction function;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

struct Registrar {
  Registrar(std::string name, TestFunction function) { registry().push_back({std::move(name), std::move(function)}); }
};

inline void require(const bool condition, const char* expression, const char* file, const int line) {
  if (!condition) throw std::runtime_error(std::string(file) + ':' + std::to_string(line) + " requirement failed: " + expression);
}

inline void requireNear(const double actual, const double expected, const double tolerance,
                        const char* file, const int line) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(std::string(file) + ':' + std::to_string(line) + " values differ: " +
                             std::to_string(actual) + " vs " + std::to_string(expected));
  }
}

}  // namespace omnidetect::test

#define OMNI_JOIN_IMPL(a, b) a##b
#define OMNI_JOIN(a, b) OMNI_JOIN_IMPL(a, b)
#define OMNI_TEST(name) \
  static void name(); \
  static ::omnidetect::test::Registrar OMNI_JOIN(registrar_, name)(#name, &name); \
  static void name()
#define OMNI_REQUIRE(expression) ::omnidetect::test::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define OMNI_REQUIRE_NEAR(actual, expected, tolerance) \
  ::omnidetect::test::requireNear((actual), (expected), (tolerance), __FILE__, __LINE__)
#endif
