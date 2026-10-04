#include <WiFi/St67NetworkAdapter.hpp>

#include "w6x_api.h"

namespace HostController {

bool St67GetStationStatus(St67StationStatus* status) {
  if (status == nullptr) {
    return false;
  }

  W6X_WiFi_StaStateType_e stationState = W6X_WIFI_STATE_STA_OFF;
  if (W6X_WiFi_Station_GetState(&stationState, nullptr) != W6X_STATUS_OK) {
    return false;
  }

  status->wifiDisconnected =
      stationState == W6X_WIFI_STATE_STA_DISCONNECTED ||
      stationState == W6X_WIFI_STATE_STA_OFF;
  status->linkUp = stationState == W6X_WIFI_STATE_STA_CONNECTED ||
                   stationState == W6X_WIFI_STATE_STA_GOT_IP;
  status->hasIpv4 = stationState == W6X_WIFI_STATE_STA_GOT_IP;
  return true;
}

}  // namespace HostController
