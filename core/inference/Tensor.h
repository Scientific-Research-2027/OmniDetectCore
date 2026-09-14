#pragma once

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace omnidetect {

struct Tensor {
  std::vector<std::int64_t> shape;
  std::vector<float> values;

  [[nodiscard]] std::size_t elementCount() const noexcept {
    if (shape.empty()) return 0;
    std::size_t count = 1;
    for (const auto dimension : shape) {
      if (dimension <= 0) return 0;
      count *= static_cast<std::size_t>(dimension);
    }
    return count;
  }

  [[nodiscard]] bool valid() const noexcept { return elementCount() == values.size(); }
};

struct TensorInfo {
  std::string name;
  std::vector<std::int64_t> shape;
};

}  // namespace omnidetect

