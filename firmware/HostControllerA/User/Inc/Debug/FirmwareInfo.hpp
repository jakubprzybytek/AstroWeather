#pragma once

// Identity of the running image, shared by the connect message and 'status' so
// the two cannot disagree.
const char* firmwareVariant();

// When the image was built, as "YYYY-MM-DD HH:MM:SS" local time. Stamped on
// every build by cmake/BuildInfo.cmake, so it matches the image even after an
// incremental build.
const char* firmwareBuildTime();
