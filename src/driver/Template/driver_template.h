#ifndef DRIVER_TEMPLATE_H
#define DRIVER_TEMPLATE_H

#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Driver Template (for reuse)
 *
 * Usage:
 * 1) Copy this file and the .c template into driver/Src and driver/Inc
 * 2) Rename symbols and types
 * 3) Implement init/start/stop and optional read/write
 *
 * IMPORTANT:
 * - Do not ship this template as-is.
 * - You MUST implement and register a real HAL ops table.
 */

int driver_template_register(void);

#ifdef __cplusplus
}
#endif

#endif /* DRIVER_TEMPLATE_H */
