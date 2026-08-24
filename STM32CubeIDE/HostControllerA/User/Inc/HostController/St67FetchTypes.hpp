#ifndef INC_HOSTCONTROLLER_ST67FETCHTYPES_HPP_
#define INC_HOSTCONTROLLER_ST67FETCHTYPES_HPP_

#include <stdint.h>

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
};

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
  St67FetchResult result{};
};

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67FETCHTYPES_HPP_ */
