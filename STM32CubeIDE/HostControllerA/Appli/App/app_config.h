#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#if defined(__has_include)
#if __has_include("app_credentials.h")
#include "app_credentials.h"
#endif
#endif

#define APP_ST67_STARTUP_DELAY_MS 4000U
#define APP_ST67_SCAN_TIMEOUT_MS 15000U
#define APP_ST67_SCAN_MAX_RESULTS 20U
#define APP_ST67_DHCP_TIMEOUT_MS 15000U
#define APP_ST67_DISCONNECT_TIMEOUT_MS 12000U
#define APP_ST67_SHUTDOWN_SETTLING_DELAY_MS 100U
#define APP_ST67_COLD_RESTART_DELAY_MS 1000U

#ifndef APP_ST67_WIFI_SSID
#define APP_ST67_WIFI_SSID ""
#endif

#ifndef APP_ST67_WIFI_PASSWORD
#define APP_ST67_WIFI_PASSWORD ""
#endif

#endif /* APP_CONFIG_H */
