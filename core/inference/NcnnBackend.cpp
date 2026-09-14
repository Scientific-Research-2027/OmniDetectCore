#include "core/inference/NcnnBackend.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>

#ifdef OMNIDETECT_HAS_NCNN
#include <net.h>
#endif

namespace omnidetect {

struct NcnnBackend::Impl {
  BackendConfig config;
  bool ready{false};
  std::vector<TensorInfo> inputs;
  std::vector<TensorInfo> outputs;
#ifdef OMNIDETECT_HAS_NCNN
  ncnn::Net network;
#endif
};

NcnnBackend::NcnnBackend() : impl_(std::make_unique<Impl>()) {}
NcnnBackend::~NcnnBackend() = default;

bool NcnnBackend::initialize(const BackendConfig& config, std::string& error) noexcept {
  shutdown();
  impl_->config = config;
#ifndef OMNIDETECT_HAS_NCNN
  error = "NCNN backend was not compiled. Reconfigure with OMNIDETECT_ENABLE_NCNN=ON and ncnn_DIR.";
  return false;
#else
  try {
    auto param = config.modelPath;
    auto weights = config.weightsPath;
    if (param.extension() != ".param" && std::filesystem::is_directory(param)) param /= "model.ncnn.param";
    if (weights.empty()) {
      weights = param;
      weights.replace_extension(".bin");
    }
    if (!std::filesystem::exists(param) || !std::filesystem::exists(weights)) {
      error = "NCNN .param or .bin file does not exist";
      return false;
    }
    impl_->network.opt.num_threads = std::max(1, config.threadCount);
    impl_->network.opt.use_vulkan_compute = config.useVulkan;
    if (impl_->network.load_param(param.string().c_str()) != 0 ||
        impl_->network.load_model(weights.string().c_str()) != 0) {
      error = "NCNN failed to load model files";
      return false;
    }
    impl_->inputs = {{config.inputName, {-1, 3, -1, -1}}};
    for (const auto& name : config.outputNames) impl_->outputs.push_back({name, {}});
    if (impl_->outputs.empty()) impl_->outputs.push_back({"output0", {}});
    impl_->ready = true;
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    shutdown();
    return false;
  }
#endif
}

BackendResult NcnnBackend::infer(const Tensor& input) noexcept {
#ifndef OMNIDETECT_HAS_NCNN
  static_cast<void>(input);
  return {false, {}, "NCNN backend is unavailable in this build"};
#else
  if (!impl_->ready) return {false, {}, "NCNN backend is not initialized"};
  if (!input.valid() || input.shape.size() != 4 || input.shape[0] != 1 || input.shape[1] != 3) {
    return {false, {}, "NCNN expects a valid NCHW float tensor with N=1 and C=3"};
  }
  try {
    const int height = static_cast<int>(input.shape[2]);
    const int width = static_cast<int>(input.shape[3]);
    ncnn::Mat image(width, height, 3);
    std::copy(input.values.begin(), input.values.end(), static_cast<float*>(image.data));
    auto extractor = impl_->network.create_extractor();
    extractor.set_num_threads(std::max(1, impl_->config.threadCount));
    if (extractor.input(impl_->config.inputName.c_str(), image) != 0) {
      return {false, {}, "NCNN rejected the configured input blob name"};
    }
    BackendResult result{true, {}, {}};
    for (const auto& output : impl_->outputs) {
      ncnn::Mat matrix;
      if (extractor.extract(output.name.c_str(), matrix) != 0) {
        return {false, {}, "NCNN failed to extract output blob: " + output.name};
      }
      Tensor tensor;
      if (matrix.dims == 1) tensor.shape = {matrix.w};
      else if (matrix.dims == 2) tensor.shape = {matrix.h, matrix.w};
      else if (matrix.dims == 3) tensor.shape = {matrix.c, matrix.h, matrix.w};
      else return {false, {}, "Unsupported NCNN output rank"};
      const auto count = matrix.total();
      const auto* begin = static_cast<const float*>(matrix.data);
      tensor.values.assign(begin, begin + count);
      result.outputs.push_back(std::move(tensor));
    }
    return result;
  } catch (const std::exception& exception) {
    return {false, {}, exception.what()};
  } catch (...) {
    return {false, {}, "NCNN inference failed with an unknown error"};
  }
#endif
}

void NcnnBackend::shutdown() noexcept {
#ifdef OMNIDETECT_HAS_NCNN
  impl_->network.clear();
#endif
  impl_->ready = false;
  impl_->inputs.clear();
  impl_->outputs.clear();
}

bool NcnnBackend::isReady() const noexcept { return impl_->ready; }
std::vector<TensorInfo> NcnnBackend::inputMetadata() const { return impl_->inputs; }
std::vector<TensorInfo> NcnnBackend::outputMetadata() const { return impl_->outputs; }

}  // namespace omnidetect

