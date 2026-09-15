#include "control/OnvifCameraControl.h"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <utility>

#ifdef OMNIDETECT_HAS_ONVIF_CURL
#include <curl/curl.h>
#endif

namespace omnidetect {
namespace {

std::string xmlEscape(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (const auto character : value) {
    if (character == '&') result += "&amp;";
    else if (character == '<') result += "&lt;";
    else if (character == '>') result += "&gt;";
    else if (character == '\"') result += "&quot;";
    else if (character == '\'') result += "&apos;";
    else result.push_back(character);
  }
  return result;
}

#ifdef OMNIDETECT_HAS_ONVIF_CURL
std::size_t discardResponse(char* data, const std::size_t size, const std::size_t count, void*) {
  static_cast<void>(data);
  return size * count;
}
#endif

}  // namespace

OnvifCameraControl::OnvifCameraControl(CameraControlConfig config) : config_(std::move(config)) {}

bool OnvifCameraControl::open(std::string& error) noexcept {
#ifndef OMNIDETECT_HAS_ONVIF_CURL
  error = "ONVIF control requires a build with OMNIDETECT_ENABLE_ONVIF=ON and libcurl";
  return false;
#else
  if ((!config_.endpoint.starts_with("http://") && !config_.endpoint.starts_with("https://")) ||
      config_.profileToken.empty()) {
    error = "ONVIF endpoint/profile token is invalid";
    return false;
  }
  static std::once_flag initialized;
  std::call_once(initialized, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
  ready_.store(true, std::memory_order_release);
  error.clear();
  return true;
#endif
}

bool OnvifCameraControl::execute(const CameraControlCommand& command, std::string& error) noexcept {
#ifndef OMNIDETECT_HAS_ONVIF_CURL
  static_cast<void>(command);
  error = "ONVIF control is unavailable in this build";
  return false;
#else
  if (!isReady()) {
    error = "ONVIF control is not ready";
    return false;
  }
  if (command.type == CameraCommandType::AbsoluteMove) {
    error = "ONVIF absolute movement is not enabled without queried device ranges";
    return false;
  }
  try {
    const auto token = xmlEscape(config_.profileToken);
    std::ostringstream body;
    body << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<s:Envelope xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\" "
            "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\" "
            "xmlns:tt=\"http://www.onvif.org/ver10/schema\"><s:Body>";
    if (command.type == CameraCommandType::ContinuousMove) {
      body << "<tptz:ContinuousMove><tptz:ProfileToken>" << token
           << "</tptz:ProfileToken><tptz:Velocity><tt:PanTilt x=\""
           << std::clamp(command.pan, -1.0F, 1.0F) << "\" y=\""
           << std::clamp(command.tilt, -1.0F, 1.0F) << "\"/><tt:Zoom x=\""
           << std::clamp(command.zoom, -1.0F, 1.0F)
           << "\"/></tptz:Velocity></tptz:ContinuousMove>";
    } else if (command.type == CameraCommandType::Home) {
      body << "<tptz:GotoHomePosition><tptz:ProfileToken>" << token
           << "</tptz:ProfileToken></tptz:GotoHomePosition>";
    } else {
      body << "<tptz:Stop><tptz:ProfileToken>" << token
           << "</tptz:ProfileToken><tptz:PanTilt>true</tptz:PanTilt>"
              "<tptz:Zoom>true</tptz:Zoom></tptz:Stop>";
    }
    body << "</s:Body></s:Envelope>";
    const auto payload = body.str();
    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
      error = "Cannot initialize ONVIF HTTP client";
      return false;
    }
    curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/soap+xml; charset=utf-8");
    const auto credentials = config_.username + ':' + config_.password;
    curl_easy_setopt(handle, CURLOPT_URL, config_.endpoint.c_str());
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
    curl_easy_setopt(handle, CURLOPT_USERPWD, credentials.c_str());
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(config_.timeout.count()));
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, discardResponse);
    const auto status = curl_easy_perform(handle);
    long responseCode = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &responseCode);
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    if (status != CURLE_OK || responseCode < 200 || responseCode >= 300) {
      error = "ONVIF camera rejected or did not answer the PTZ command";
      return false;
    }
    error.clear();
    return true;
  } catch (...) {
    error = "ONVIF adapter failed while building or sending a command";
    return false;
  }
#endif
}

void OnvifCameraControl::close() noexcept { ready_.store(false, std::memory_order_release); }

CameraControlCapabilities OnvifCameraControl::capabilities() const noexcept {
  return {true, true, false, true};
}

}  // namespace omnidetect
