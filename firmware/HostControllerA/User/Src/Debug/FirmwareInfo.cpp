#include <Debug/FirmwareInfo.hpp>

const char* firmwareVariant()
{
    return "HostController";
}

// Defined in BuildInfo.cpp, which the build regenerates every time.
extern const char* const kFirmwareBuildTime;

const char* firmwareBuildTime()
{
    return kFirmwareBuildTime;
}
