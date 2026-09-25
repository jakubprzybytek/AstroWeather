#ifndef INC_HOSTCONTROLLER_ST67CONNECTDIAGNOSIS_HPP_
#define INC_HOSTCONTROLLER_ST67CONNECTDIAGNOSIS_HPP_

#include <WiFi/St67HttpFetchTask.hpp>

#include <stdint.h>

// Turns a failed station connect into a WifiConnectResult and the one
// plain-language line logged for it. No driver calls, so the native tests run
// it; St67NetworkSession feeds it the reason code and the scan outcome.

namespace HostController {
namespace St67ConnectDiagnosis {

// Stored before each connect so a failure with no reason event can be told
// apart from one that reported WLAN_FW_SUCCESSFUL (0).
constexpr uint32_t kNoReason = 0xFFFFFFFFU;

// The module reason codes the classification looks at (WLAN_FW_* in
// w6x_types.h; St67NetworkSession.cpp checks they still match).
constexpr uint32_t kReasonAuthenticationFailure = 2U;
constexpr uint32_t kReasonAuthAlgoFailure = 3U;
constexpr uint32_t kReasonDeauthByApWhenConnection = 7U;
constexpr uint32_t kReasonPskHandshakeTimeout = 8U;
constexpr uint32_t kReasonNoBssidAndChannel = 12U;
constexpr uint32_t kReasonSecurityNoMatch = 17U;

WifiConnectResult classifyConnectFailure(uint32_t reason);

// What an SSID scan after a silent timeout found.
enum class SsidVisibility : uint8_t { Visible, NotVisible, Unknown };

// A timeout with no reason (NoResponse) is ambiguous: scanning for the SSID
// tells a missing network from a silent one.
bool needsScan(WifiConnectResult result);
WifiConnectResult applyScan(WifiConnectResult result, SsidVisibility visibility);

// The arguments failureMessageFormat()'s string takes, in order.
enum class MessageArguments : uint8_t {
  None,           // plain text
  Ssid,           // (const char* ssid)
  SsidAndReason,  // (const char* ssid, const char* reasonText, unsigned long reason)
};

// The printf format of the line logged for a failed connect. The password is
// never an argument.
const char* failureMessageFormat(WifiConnectResult result);
MessageArguments failureMessageArguments(WifiConnectResult result);

}  // namespace St67ConnectDiagnosis
}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67CONNECTDIAGNOSIS_HPP_ */
