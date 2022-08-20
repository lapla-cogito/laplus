#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern void e1000_intr(void);
extern void e1000_init(uintptr_t mmio_base);

#ifdef __cplusplus
}
#endif
