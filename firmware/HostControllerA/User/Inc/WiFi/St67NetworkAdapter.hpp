#ifndef INC_HOSTCONTROLLER_ST67NETWORKADAPTER_HPP_
#define INC_HOSTCONTROLLER_ST67NETWORKADAPTER_HPP_

#include <stdint.h>

namespace HostController {

// The station as the module reports it (W6X_WiFi_Station_GetState). With the
// TCP/IP stack in the module there is no host netif to look at.
struct St67StationStatus {
  bool wifiDisconnected;  // disconnected or off
  bool linkUp;            // associated, with or without an address
  bool hasIpv4;           // DHCP finished
};

bool St67GetStationStatus(St67StationStatus* status);

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67NETWORKADAPTER_HPP_ */
