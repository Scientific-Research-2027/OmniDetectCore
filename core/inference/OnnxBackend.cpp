#include "core/inference/OnnxBackend.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

#ifdef OMNIDETECT_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

namespace omnidetect {

struct OnnxBackend::Impl {
  BackendConfig config;
  bool ready{false};
  std::vector<TensorInfo> inputs;
  std::vector<TensorInfo> outputs;
#ifdef OMNIDETECT_HAS_ONNX
  Ort::Env environment{ORT_LOGGING_LEVEL_WARNING, "OmniDetectCore"};
  std::unique_ptr<Ort::Session> session;
  std::vector<std::string> inputNames;
  std::vector<std::string> outputNames;
  std::size_t selectedInput{0};
#endif
};

OnnxBackend::OnnxBackend() : impl_(std::make_unique<Impl>()) {}
OnnxBackend::~OnnxBackend() = default;

bool OnnxBackend::initialize(const BackendConfig& config, std::string& error) noexcept {
  shutdown();
  impl_->config = config;
#ifndef OMNIDETECT_HAS_ONNX
  error = "ONNX Runtime backend was not compiled. Reconfigure with OMNIDETECT_ENABLE_ONNX=ON and onnxruntime_DIR.";
  return false;
#else
  try {
    if (!std::filesystem::exists(config.modelPath)) {
      error = "ONNX model file does not exist: " + config.modelPath.string();
      return false;
    }
    Ort::SessionOptions options;
    options.SetIntraOpNumThreads(std::max(1, config.threadCount));
    options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef _WIN32
    impl_->session = std::make_unique<Ort::Session>(impl_->environment, config.modelPath.wstring().c_str(), options);
#else
    impl_->session = std::make_unique<Ort::Session>(impl_->environment, config.modelPath.string().c_str(), options);
#endif
    Ort::AllocatorWithDefaultOptions allocator;
    for (std::size_t i = 0; i < impl_->session->GetInputCount(); ++i) {
      auto name = impl_->session->GetInputNameAllocated(i, allocator);
      impl_->inputNames.emplace_back(name.get());
      impl_->inputs.push_back({impl_->inputNames.back(), impl_->session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape()});
    }
    for (std::size_t i = 0; i < impl_->session->GetOutputCount(); ++i) {
      auto name = impl_->session->GetOutputNameAllocated(i, allocator);
      impl_->outputNames.emplace_back(name.get());
      impl_->outputs.push_back({impl_->outputNames.back(), impl_->session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape()});
    }
    if (!config.inputName.empty()) {
      const auto found = std::find(impl_->inputNames.begin(), impl_->inputNames.end(), config.inputName);
      if (found == impl_->inputNames.end()) {
        error = "ONNX input name not found: " + config.inputName;
        shutdown();
        return false;
      }
      impl_->selectedInput = static_cast<std::size_t>(std::distance(impl_->inputNames.begin(), found));
    }
    if (!config.outputNames.empty()) {
      for (const auto& requested : config.outputNames) {
        if (std::find(impl_->outputNames.begin(), impl_->outputNames.end(), requested) == impl_->outputNames.end()) {
          error = "ONNX output name not found: " + requested;
          shutdown();
          return false;
        }
      }
      impl_->outputNames = config.outputNames;
      impl_->outputs.clear();
      for (const auto& requested : impl_->outputNames) impl_->outputs.push_back({requested, {}});
    }
    impl_->ready = true;
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    shutdown();
    return false;
  }
#endif
}

BackendResult OnnxBackend::infer(const Tensor& input) noexcept {
#ifndef OMNIDETECT_HAS_ONNX
  static_cast<void>(input);
  return {false, {}, "ONNX Runtime backend is unavailable in this build"};
#else
  if (!impl_->ready || !impl_->session) return {false, {}, "ONNX Runtime backend is not initialized"};
  if (!input.valid()) return {false, {}, "Invalid input tensor"};
  try {
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    auto value = Ort::Value::CreateTensor<float>(memory, const_cast<float*>(input.values.data()), input.values.size(),
                                                 input.shape.data(), input.shape.size());
    const char* inputName = impl_->inputNames[impl_->selectedInput].c_str();
    std::vector<const char*> outputNames;
    outputNames.reserve(impl_->outputNames.size());
    for (const auto& name : impl_->outputNames) outputNames.push_back(name.c_str());
    auto values = impl_->session->Run(Ort::RunOptions{nullptr}, &inputName, &value, 1,
                                      outputNames.data(), outputNames.size());
    BackendResult result{true, {}, {}};
    for (auto& ortValue : values) {
      if (!ortValue.IsTensor()) return {false, {}, "ONNX output is not a tensor"};
      const auto information = ortValue.GetTensorTypeAndShapeInfo();
      if (information.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
        return {false, {}, "Only float32 ONNX output tensors are supported by this backend"};
      }
      Tensor tensor;
      tensor.shape = information.GetShape();
      const auto count = information.GetElementCount();
      const auto* data = ortValue.GetTensorData<float>();
      tensor.values.assign(data, data + count);
      result.outputs.push_back(std::move(tensor));
    }
    return result;
  } catch (const std::exception& exception) {
    return {false, {}, exception.what()};
  } catch (...) {
    return {false, {}, "ONNX Runtime inference failed with an unknown error"};
  }
#endif
}

void OnnxBackend::shutdown() noexcept {
#ifdef OMNIDETECT_HAS_ONNX
  impl_->session.reset();
  impl_->inputNames.clear();
  impl_->outputNames.clear();
  impl_->selectedInput = 0;
#endif
  impl_->inputs.clear();
  impl_->outputs.clear();
  impl_->ready = false;
}

bool OnnxBackend::isReady() const noexcept { return impl_->ready; }
std::vector<TensorInfo> OnnxBackend::inputMetadata() const { return impl_->inputs; }
std::vector<TensorInfo> OnnxBackend::outputMetadata() const { return impl_->outputs; }

}  // namespace omnidetect
