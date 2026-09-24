#ifndef INC_HOSTCONTROLLER_ST67HTTPFETCHTASK_HPP_
#define INC_HOSTCONTROLLER_ST67HTTPFETCHTASK_HPP_

#include <St67FetchTypes.hpp>

#include <cstdint>

namespace Settings {
class Store;
}

namespace HostController {

// Where the station takes its SSID and password, and the fetch its API host
// and path, from. Read on every connect and fetch, so 'wifi set' and 'api ...'
// take effect on the next attempt. Set before the fetch task starts; with no
// source, or no SSID stored, connecting fails as NoCredentials, and the API
// target falls back to the built-in one.
void SetSt67CredentialSource(Settings::Store* store);
// The store set above, or null.
Settings::Store* St67CredentialSource();

// Why the last station connect ended the way it did, classified from the
// module's Wi-Fi reason code so the console can say what to fix.
enum class WifiConnectResult : uint8_t {
  NeverTried,        // no connect attempted since boot
  Connected,
  NoCredentials,     // no SSID stored
  NetworkNotFound,   // timed out, and a scan found no access point with that SSID
  WrongPassword,     // WPA handshake failed: almost always the passphrase
  SecurityMismatch,  // rejected at authentication, e.g. security type differs
  NoResponse,        // timed out with no reason, yet the SSID shows in a scan
  NoResponseUnchecked,  // timed out with no reason, and the scan could not run
  DhcpFailed,        // joined the network but was given no IP address
  Failed,            // some other reason; see reason and reasonText
};

struct WifiConnectSummary {
  WifiConnectResult result = WifiConnectResult::NeverTried;
  uint32_t reason = 0U;         // module reason code, when one was reported
  const char* reasonText = "";  // driver's name for that code
  uint32_t tick = 0U;           // osKernelGetTickCount() of the attempt
  int32_t rssi = 0;             // dBm, when connected
  uint32_t channel = 0U;        // when connected
  char ssid[33] = {};           // SSID used for the attempt
};

// Safe to call from any task.
WifiConnectSummary LastWifiConnect();
const char* wifiConnectResultName(WifiConnectResult result);

void StartSt67HttpFetchTask();
void TriggerSt67SmokeTest();
void TriggerSt67ConnectivityCycle();
// Runs one fetch on the WiFi task and waits for it on the calling thread,
// calling onProgress (if given) on stage changes and every 250 ms. Uses the
// kFetchFlagDone/kFetchFlagStage thread flags of the calling thread.
bool FetchSt67Data(St67FetchRequest* request, FetchProgressFn onProgress = nullptr,
                   void* context = nullptr);

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67HTTPFETCHTASK_HPP_ */