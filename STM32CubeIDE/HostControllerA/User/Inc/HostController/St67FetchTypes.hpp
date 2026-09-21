#ifndef INC_HOSTCONTROLLER_ST67FETCHTYPES_HPP_
#define INC_HOSTCONTROLLER_ST67FETCHTYPES_HPP_

#include <stdint.h>

#include "cmsis_os2.h"

namespace HostController {

enum class St67FetchStatus : uint8_t {
  Success,
  Busy,
  InvalidArgument,
  DriverFailure,
  NetworkFailure,
  HttpFailure,
  ResponseTooLarge,
  CleanupFailure,
  NoCredentials,  // no SSID stored; see 'wifi set'
  Timeout,        // the WiFi task did not finish within kClientFetchTimeoutMs
};

// Where a client fetch has got to, for progress reporting. Advanced by the WiFi
// task and frozen at the first failure, so after a failed fetch it names the
// step that failed rather than the cleanup that followed.
enum class FetchStage : uint8_t {
  Queued,          // accepted, not yet started
  StartingModule,  // powering up and initialising the ST67 (first fetch after boot)
  JoiningWifi,
  GettingIp,       // DHCP
  Downloading,     // DNS and the HTTP request
  Disconnecting,
};

// Thread flags FetchSt67Data uses on the *calling* thread: the WiFi task sets
// them to report a finished fetch or a new stage. Callers must not use these
// bits for anything else.
constexpr uint32_t kFetchFlagDone = 1U << 24;
constexpr uint32_t kFetchFlagStage = 1U << 25;

// Upper bound on one client fetch. The longest legitimate run seen on the bench
// was about 2 minutes: the first fetch after boot, including module start-up.
constexpr uint32_t kClientFetchTimeoutMs = 180000U;

struct St67FetchResult {
  St67FetchStatus status = St67FetchStatus::InvalidArgument;
  uint16_t httpStatus = 0U;
  uint32_t length = 0U;
  uint32_t crc32 = 0U;
  int32_t detail = 0;
};

struct St67FetchRequest {
  uint8_t* buffer = nullptr;
  uint32_t capacity = 0U;
  volatile bool completed = false;
  volatile FetchStage stage = FetchStage::Queued;
  osThreadId_t waiter = nullptr;  // thread to signal; set by FetchSt67Data
  St67FetchResult result{};
};

// Called on the fetching thread when the stage changes, and periodically while
// waiting, so the caller can animate progress. May be null.
using FetchProgressFn = void (*)(FetchStage stage, void* context);

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67FETCHTYPES_HPP_ */
