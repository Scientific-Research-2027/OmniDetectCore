#include "core/inference/BackendFactory.h"

#include "core/inference/NcnnBackend.h"
#include "core/inference/OnnxBackend.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace omnidetect {
namespace {
std::string normalized(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
  return value;
}
}  // namespace

BackendFactory::BackendFactory() {
  creators_.emplace("ncnn", [] { return std::make_unique<NcnnBackend>(); });
  creators_.emplace("onnx", [] { return std::make_unique<OnnxBackend>(); });
}

BackendFactory& BackendFactory::instance() {
  static BackendFactory factory;
  return factory;
}

bool BackendFactory::registerBackend(std::string name, Creator creator) {
  if (name.empty() || !creator) return false;
  const std::scoped_lock lock(mutex_);
  creators_.insert_or_assign(normalized(std::move(name)), std::move(creator));
  return true;
}

std::unique_ptr<IInferenceBackend> BackendFactory::create(const std::string& name) const {
  const std::scoped_lock lock(mutex_);
  const auto found = creators_.find(normalized(name));
  return found == creators_.end() ? nullptr : found->second();
}

std::vector<std::string> BackendFactory::available() const {
  const std::scoped_lock lock(mutex_);
  std::vector<std::string> names;
  names.reserve(creators_.size());
  for (const auto& [name, unused] : creators_) {
    static_cast<void>(unused);
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

}  // namespace omnidetect
