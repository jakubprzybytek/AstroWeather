#pragma once

#include <cstdint>

// Identity of the running image, shared by the connect message and 'status' so
// the two cannot disagree.
const char* firmwareVariant();

// When the image was built, as "YYYY-MM-DD HH:MM:SS" local time. Stamped on
// every build by Common/cmake/BuildInfo.cmake, so it matches the image even
// after an incremental build.
const char* firmwareBuildTime();

// The build's number: one more on every build, from the committed BUILD_NUMBER.
uint32_t firmwareBuildNumber();
