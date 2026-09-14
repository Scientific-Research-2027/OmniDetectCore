#pragma once

#include "core/inference/Tensor.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnidetect {

struct BackendConfig {
  std::filesystem::path modelPath;
  std::filesystem::path weightsPath;
  std::string inputName{"images"};
  std::vector<std::string> outputNames;
  int threadCount{1};
  bool useVulkan{false};
  std::unordered_map<std::string, std::string> options;
};

struct BackendResult {
  bool success{false};
  std::vector<Tensor> outputs;
  std::string error;
};

class IInferenceBackend {
 public:
  virtual ~IInferenceBackend() = default;
  virtual bool initialize(const BackendConfig& config, std::string& error) noexcept = 0;
  virtual BackendResult infer(const Tensor& input) noexcept = 0;
  virtual void shutdown() noexcept = 0;
  [[nodiscard]] virtual bool isReady() const noexcept = 0;
  [[nodiscard]] virtual std::string name() const = 0;
  [[nodiscard]] virtual std::vector<TensorInfo> inputMetadata() const = 0;
  [[nodiscard]] virtual std::vector<TensorInfo> outputMetadata() const = 0;
};

}  // namespace omnidetect

