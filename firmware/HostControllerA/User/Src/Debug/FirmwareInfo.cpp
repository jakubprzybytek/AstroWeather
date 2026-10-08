#include <Debug/FirmwareInfo.hpp>

#include <Debug/BuildInfo.hpp>

const char* firmwareVariant()
{
    return "HostController";
}

const char* firmwareBuildTime()
{
    return kFirmwareBuildTime;
}

uint32_t firmwareBuildNumber()
{
    return kFirmwareBuildNumber;
}
