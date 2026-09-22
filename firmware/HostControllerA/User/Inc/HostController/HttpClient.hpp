#ifndef INC_HOSTCONTROLLER_HTTPCLIENT_HPP_
#define INC_HOSTCONTROLLER_HTTPCLIENT_HPP_

#include <stdint.h>

#include "http_client.h"
#include "lwip/ip_addr.h"

namespace HostController {

int32_t HttpClient_Get(const ip_addr_t* serverAddress, uint16_t port,
                       const char* host, const char* path,
                       const HTTP_connection_t* settings);

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_HTTPCLIENT_HPP_ */
