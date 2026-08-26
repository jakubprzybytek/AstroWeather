#include <HostController/St67NetworkAdapter.hpp>

#include "lwip.h"
#include "lwip/netif.h"
#include "w6x_api.h"

namespace HostController {

bool St67GetStationStatus(St67StationStatus* status) {
  if (status == nullptr) {
    return false;
  }

  W6X_WiFi_StaStateType_e stationState = W6X_WIFI_STATE_STA_OFF;
  struct netif* station = netif_get_interface(NETIF_STA);
  if (station == nullptr ||
      W6X_WiFi_Station_GetState(&stationState, nullptr) != W6X_STATUS_OK) {
    return false;
  }

  status->wifiDisconnected =
      stationState == W6X_WIFI_STATE_STA_DISCONNECTED ||
      stationState == W6X_WIFI_STATE_STA_OFF;
  status->interfaceUp = netif_is_up(station);
  status->linkUp = netif_is_link_up(station);
  status->hasIpv4 = !ip4_addr_isany_val(*netif_ip4_addr(station));
  return true;
}

bool St67NetworkInterfacesReady() {
  return netif_get_interface(NETIF_STA) != nullptr &&
         netif_get_interface(NETIF_AP) != nullptr;
}

}  // namespace HostController
