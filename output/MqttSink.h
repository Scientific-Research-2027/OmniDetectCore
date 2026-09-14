#pragma once

#include "output/IResultSink.h"

#include <string>
#include <memory>
#include <utility>

namespace omnidetect {

struct MqttConfig {
  std::string brokerUri;
  std::string topic{"omnidetect/events"};
  std::string clientId{"omnidetect-edge"};
};

class IMqttClient {
 public:
  virtual ~IMqttClient() = default;
  virtual bool connect(const MqttConfig& config, std::string& error) noexcept = 0;
  virtual bool publish(const std::string& topic, const std::string& payload, std::string& error) noexcept = 0;
  virtual void disconnect() noexcept = 0;
};

class MqttSink final : public IResultSink {
 public:
  explicit MqttSink(MqttConfig config, std::shared_ptr<IMqttClient> client = {})
      : config_(std::move(config)), client_(std::move(client)) {}
  bool start(std::string& error) noexcept override;
  void consume(const Frame& frame, const DetectionResult& result) noexcept override;
  void stop() noexcept override;
  [[nodiscard]] std::string name() const override { return "mqtt"; }

 private:
  MqttConfig config_;
  std::shared_ptr<IMqttClient> client_;
  bool connected_{false};
};

}  // namespace omnidetect
