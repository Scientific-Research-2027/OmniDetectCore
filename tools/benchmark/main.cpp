#include "core/inference/IInferenceBackend.h"
#include "core/yolo/Yolo26Detector.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <vector>

namespace {

class BenchmarkBackend final : public omnidetect::IInferenceBackend {
 public:
  bool initialize(const omnidetect::BackendConfig&, std::string& error) noexcept override {
    ready_ = true; error.clear(); return true;
  }
  omnidetect::BackendResult infer(const omnidetect::Tensor&) noexcept override {
    return {true, {{{1, 6}, {0, 0, 0, 0, 0, 0}}}, {}};
  }
  void shutdown() noexcept override { ready_ = false; }
  [[nodiscard]] bool isReady() const noexcept override { return ready_; }
  [[nodiscard]] std::string name() const override { return "benchmark-noop"; }
  [[nodiscard]] std::vector<omnidetect::TensorInfo> inputMetadata() const override { return {}; }
  [[nodiscard]] std::vector<omnidetect::TensorInfo> outputMetadata() const override { return {}; }
 private:
  bool ready_{false};
};

omnidetect::Frame syntheticFrame() {
  auto image = std::make_shared<omnidetect::ImageBuffer>();
  image->width = 1280;
  image->height = 720;
  image->channels = 3;
  image->stride = 1280U * 3U;
  image->format = omnidetect::PixelFormat::Bgr8;
  image->bytes.resize(image->stride * 720U);
  for (std::size_t index = 0; index < image->bytes.size(); ++index) {
    image->bytes[index] = static_cast<std::uint8_t>(index % 251U);
  }
  return {std::move(image), 1, std::chrono::steady_clock::now(), std::chrono::system_clock::now(), "benchmark", {}};
}

double percentile(std::vector<double> values, const double quantile) {
  std::sort(values.begin(), values.end());
  const auto index = static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(values.size()))) - 1U;
  return values[std::min(index, values.size() - 1U)];
}

}  // namespace

int main() {
  constexpr int warmup = 2;
  constexpr int measured = 10;
  const auto frame = syntheticFrame();
  std::cout << "Portable preprocessing/postprocessing benchmark (noop inference)\n";
  std::cout << "input  average_ms  median_ms  p95_ms  fps\n";
  for (const int inputSize : {320, 416, 640}) {
    omnidetect::DetectorConfig config;
    config.inputWidth = inputSize;
    config.inputHeight = inputSize;
    config.confidenceThreshold = 0.5F;
    omnidetect::Yolo26Detector detector(config, std::make_unique<BenchmarkBackend>(), {});
    for (int index = 0; index < warmup; ++index) static_cast<void>(detector.detect(frame));
    std::vector<double> samples;
    samples.reserve(measured);
    for (int index = 0; index < measured; ++index) {
      const auto begin = std::chrono::steady_clock::now();
      static_cast<void>(detector.detect(frame));
      samples.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count());
    }
    const auto average = std::accumulate(samples.begin(), samples.end(), 0.0) / static_cast<double>(samples.size());
    std::cout << std::setw(5) << inputSize << "  " << std::fixed << std::setprecision(3)
              << std::setw(10) << average << "  " << std::setw(9) << percentile(samples, 0.5)
              << "  " << std::setw(6) << percentile(samples, 0.95) << "  " << std::setw(8)
              << (average > 0.0 ? 1000.0 / average : 0.0) << '\n';
  }
  return 0;
}
