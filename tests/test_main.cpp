#include "tests/TestHarness.h"

int main(int argc, char** argv) {
#ifdef OMNIDETECT_USE_GTEST
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
#else
  static_cast<void>(argc);
  static_cast<void>(argv);
  std::size_t failures = 0;
  for (const auto& testCase : omnidetect::test::registry()) {
    try {
      testCase.function();
      std::cout << "[PASS] " << testCase.name << '\n';
    } catch (const std::exception& exception) {
      ++failures;
      std::cerr << "[FAIL] " << testCase.name << ": " << exception.what() << '\n';
    } catch (...) {
      ++failures;
      std::cerr << "[FAIL] " << testCase.name << ": unknown exception\n";
    }
  }
  std::cout << omnidetect::test::registry().size() - failures << "/"
            << omnidetect::test::registry().size() << " tests passed\n";
  return failures == 0 ? 0 : 1;
#endif
}
