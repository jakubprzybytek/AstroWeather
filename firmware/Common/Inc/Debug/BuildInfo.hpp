#pragma once

#include <cstdint>

// The image's build, defined in the BuildInfo.cpp that Common/cmake/BuildInfo.cmake
// generates on every firmware build (not in the native tests).

// When the image was built, as "YYYY-MM-DD HH:MM:SS" local time.
extern const char* const kFirmwareBuildTime;
// One more on every build of this firmware, counted in the project's committed
// BUILD_NUMBER file.
extern const uint32_t kFirmwareBuildNumber;
