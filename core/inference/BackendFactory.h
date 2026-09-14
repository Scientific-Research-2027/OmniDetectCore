#pragma once

#include "core/inference/IInferenceBackend.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace omnidetect {

class BackendFactory {
 public:
  using Creator = std::function<std::unique_ptr<IInferenceBackend>()>;

  static BackendFactory& instance();
  bool registerBackend(std::string name, Creator creator);
  [[nodiscard]] std::unique_ptr<IInferenceBackend> create(const std::string& name) const;
  [[nodiscard]] std::vector<std::string> available() const;

 private:
  BackendFactory();
  mutable std::mutex mutex_;
  std::unordered_map<std::string, Creator> creators_;
};

}  // namespace omnidetect

