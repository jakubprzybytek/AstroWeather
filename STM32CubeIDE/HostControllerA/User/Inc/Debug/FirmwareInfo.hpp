#pragma once

// Identity of the running image, shared by the connect message and 'status' so
// the two cannot disagree.
const char* firmwareVariant();

// __DATE__ __TIME__ of FirmwareInfo.cpp. It only changes when that file is
// recompiled, so after an incremental build it can be older than the image.
const char* firmwareBuildTime();
