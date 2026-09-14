#pragma once

#include "core/inference/IInferenceBackend.h"

#include <memory>

namespace omnidetect {

class OnnxBackend final : public IInferenceBackend {
 public:
  OnnxBackend();
  ~OnnxBackend() override;
  bool initialize(const BackendConfig& config, std::string& error) noexcept override;
  BackendResult infer(const Tensor& input) noexcept override;
  void shutdown() noexcept override;
  [[nodiscard]] bool isReady() const noexcept override;
  [[nodiscard]] std::string name() const override { return "onnx"; }
  [[nodiscard]] std::vector<TensorInfo> inputMetadata() const override;
  [[nodiscard]] std::vector<TensorInfo> outputMetadata() const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace omnidetect
