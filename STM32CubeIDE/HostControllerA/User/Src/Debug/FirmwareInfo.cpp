#include <Debug/FirmwareInfo.hpp>

const char* firmwareVariant()
{
#if defined(FIRMWARE_VARIANT_HostController)
    return "HostController";
#else
    return "DisplayController";
#endif
}

// Defined in BuildInfo.cpp, which the build regenerates every time.
extern const char* const kFirmwareBuildTime;

const char* firmwareBuildTime()
{
    return kFirmwareBuildTime;
}
