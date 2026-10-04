#ifndef _LOG_PRINT_H
#define _LOG_PRINT_H

/* Minimal stand-in for keystone3-firmware's log_print.h (only what the touch drivers use). */
#include <stdint.h>

void PrintArray(const char *name, const uint8_t *data, uint16_t length);

#endif
