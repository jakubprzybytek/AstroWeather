#include <Debug/FirmwareInfo.hpp>

const char* firmwareVariant()
{
#if defined(FIRMWARE_VARIANT_HostController)
    return "HostController";
#else
    return "DisplayController";
#endif
}

const char* firmwareBuildTime()
{
    return __DATE__ " " __TIME__;
}
