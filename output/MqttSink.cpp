#include "output/MqttSink.h"

#include "core/logging/Logger.h"

#include <sstream>

namespace omnidetect {

bool MqttSink::start(std::string& error) noexcept {
  if (!client_) {
    error = "No MQTT client adapter was injected; link a broker library behind IMqttClient";
    return false;
  }
  if (config_.brokerUri.empty() || config_.topic.empty()) {
    error = "MQTT broker URI and topic must not be empty";
    return false;
  }
  connected_ = client_->connect(config_, error);
  return connected_;
}

void MqttSink::consume(const Frame& frame, const DetectionResult& result) noexcept {
  static_cast<void>(frame);
  if (!connected_ || !client_) return;
  std::ostringstream payload;
  payload << "{\"frame_id\":" << result.frameId << ",\"source_id\":\"" << result.sourceId
          << "\",\"detections\":" << result.detections.size() << ",\"status\":"
          << static_cast<int>(result.status) << '}';
  std::string error;
  if (!client_->publish(config_.topic, payload.str(), error)) {
    log::warning("MQTT publish failed: " + error);
  }
}

void MqttSink::stop() noexcept {
  if (client_) client_->disconnect();
  connected_ = false;
}

}  // namespace omnidetect
