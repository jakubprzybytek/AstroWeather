#include <WiFi/St67FetchStatusMap.hpp>

#include <cstring>

namespace HostController {

St67FetchStatus fetchStatusForFailure(const char* firstFailureStage, bool responseTooLarge) {
  if (firstFailureStage == nullptr) {
    return St67FetchStatus::Success;
  }
  if (responseTooLarge) {
    return St67FetchStatus::ResponseTooLarge;
  }
  if (std::strcmp(firstFailureStage, "netif-stop") == 0 ||
      std::strcmp(firstFailureStage, "final-state") == 0) {
    return St67FetchStatus::CleanupFailure;
  }
  if (std::strcmp(firstFailureStage, "credentials") == 0) {
    return St67FetchStatus::NoCredentials;
  }
  if (std::strcmp(firstFailureStage, "connect") == 0 ||
      std::strcmp(firstFailureStage, "connect-state") == 0 ||
      std::strcmp(firstFailureStage, "dhcp") == 0) {
    // Station never got online; LastWifiConnect() holds the reason.
    return St67FetchStatus::NetworkFailure;
  }
  if (std::strcmp(firstFailureStage, "w6x-init") == 0 ||
      std::strcmp(firstFailureStage, "wifi-init") == 0) {
    return St67FetchStatus::DriverFailure;
  }
  return St67FetchStatus::HttpFailure;
}

}  // namespace HostController
