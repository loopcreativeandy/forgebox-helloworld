#include <stdio.h>
#include "log_print.h"

void PrintArray(const char *name, const uint8_t *data, uint16_t length)
{
    printf("%s, length=%u\r\n", name, length);
    for (uint16_t i = 0; i < length; i++) {
        printf("%02X%s", data[i], ((i + 1U) % 32U == 0U) ? "\r\n" : " ");
    }
    printf("\r\n");
}
