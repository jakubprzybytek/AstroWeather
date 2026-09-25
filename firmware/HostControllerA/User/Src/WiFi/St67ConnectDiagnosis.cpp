#include <WiFi/St67ConnectDiagnosis.hpp>

namespace HostController {
namespace St67ConnectDiagnosis {

WifiConnectResult classifyConnectFailure(uint32_t reason) {
  switch (reason) {
    case kNoReason:
      return WifiConnectResult::NoResponse;
    case kReasonNoBssidAndChannel:
      return WifiConnectResult::NetworkNotFound;
    // Seen on hardware: a wrong WPA2 password makes the access point deauth the
    // station mid-handshake (7). Some access points let the handshake time out
    // instead (8).
    case kReasonDeauthByApWhenConnection:
    case kReasonPskHandshakeTimeout:
      return WifiConnectResult::WrongPassword;
    case kReasonAuthenticationFailure:
    case kReasonAuthAlgoFailure:
    case kReasonSecurityNoMatch:
      return WifiConnectResult::SecurityMismatch;
    default:
      return WifiConnectResult::Failed;
  }
}

bool needsScan(WifiConnectResult result) { return result == WifiConnectResult::NoResponse; }

WifiConnectResult applyScan(WifiConnectResult result, SsidVisibility visibility) {
  if (!needsScan(result)) {
    return result;
  }
  if (visibility == SsidVisibility::NotVisible) {
    return WifiConnectResult::NetworkNotFound;
  }
  if (visibility == SsidVisibility::Unknown) {
    return WifiConnectResult::NoResponseUnchecked;
  }
  return result;
}

// One plain-language line per outcome, saying what to check.
const char* failureMessageFormat(WifiConnectResult result) {
  switch (result) {
    case WifiConnectResult::NoCredentials:
      return "WiFi not configured: no SSID stored. Set one with 'wifi set <ssid> <password>'.";
    case WifiConnectResult::NetworkNotFound:
      return "WiFi network '%s' not found: no access point with that name is in range. "
             "Check the SSID; it is case-sensitive.";
    case WifiConnectResult::WrongPassword:
      return "WiFi '%s' rejected the connection during the password check, which almost "
             "always means a wrong password. Re-enter it with 'wifi set'.";
    case WifiConnectResult::SecurityMismatch:
      return "WiFi '%s' refused authentication. Check the password, and that the network "
             "uses WPA2 (or is open when no password is set).";
    case WifiConnectResult::NoResponse:
      return "WiFi '%s' is in range but did not answer the connection request. Try again, "
             "or restart the access point.";
    case WifiConnectResult::NoResponseUnchecked:
      return "WiFi '%s' did not answer before the connect timeout. Check the SSID (it is "
             "case-sensitive) and that the access point is on and in range.";
    case WifiConnectResult::DhcpFailed:
      return "WiFi joined '%s' but got no IP address from DHCP. Check the router's DHCP "
             "server.";
    default:
      return "WiFi '%s' connect failed: %s (reason %lu).";
  }
}

MessageArguments failureMessageArguments(WifiConnectResult result) {
  switch (result) {
    case WifiConnectResult::NoCredentials:
      return MessageArguments::None;
    case WifiConnectResult::NetworkNotFound:
    case WifiConnectResult::WrongPassword:
    case WifiConnectResult::SecurityMismatch:
    case WifiConnectResult::NoResponse:
    case WifiConnectResult::NoResponseUnchecked:
    case WifiConnectResult::DhcpFailed:
      return MessageArguments::Ssid;
    default:
      return MessageArguments::SsidAndReason;
  }
}

}  // namespace St67ConnectDiagnosis
}  // namespace HostController
