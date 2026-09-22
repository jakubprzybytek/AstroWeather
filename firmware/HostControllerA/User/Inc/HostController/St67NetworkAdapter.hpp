#ifndef INC_HOSTCONTROLLER_ST67NETWORKADAPTER_HPP_
#define INC_HOSTCONTROLLER_ST67NETWORKADAPTER_HPP_

#include <stdint.h>

namespace HostController {

struct St67StationStatus {
  bool wifiDisconnected;
  bool interfaceUp;
  bool linkUp;
  bool hasIpv4;
};

bool St67GetStationStatus(St67StationStatus* status);
bool St67NetworkInterfacesReady();

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67NETWORKADAPTER_HPP_ */
