#ifndef ST67_MODE_H
#define ST67_MODE_H

#include <stdint.h>

typedef enum {
  ST67_MODE_MANUFACTURE = 0u,
  ST67_MODE_BOOTLOADER = 1u
} St67Mode;

#define ST67_DEFAULT_MODE_MANUFACTURE 0u
#define ST67_DEFAULT_MODE_BOOTLOADER  1u

#ifndef ST67_DEFAULT_MODE
#define ST67_DEFAULT_MODE ST67_DEFAULT_MODE_MANUFACTURE
#endif

#if (ST67_DEFAULT_MODE == ST67_DEFAULT_MODE_BOOTLOADER)
#error "ST67 bootloader mode straps are not verified yet."
#endif

void ST67_EnterMode(St67Mode mode);

#endif /* ST67_MODE_H */
