/* ForgeBox emulator: the firmware's EAPDU + wallet code on the host.
 * stdin/stdout carry USB packets as [1-byte length][bytes]; same frames as the real device. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "eapdu.h"
#include "sol_key.h"
#include "test_mnemonic.h"

void UsbSend(const uint8_t *data, uint32_t len)
{
    uint8_t n = (uint8_t)len;
    fwrite(&n, 1, 1, stdout);
    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

void UsbSetStatus(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[status] ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

int main(void)
{
    uint8_t len;
    uint8_t frame[64];
    EapduInit();
    SolKeyLoad(TEST_MNEMONIC, 0);
    fprintf(stderr, "[emulator] address %s\n", SolKeyAddress());
    while (fread(&len, 1, 1, stdin) == 1) {
        if (len == 0 || len > sizeof(frame) || fread(frame, 1, len, stdin) != len) {
            break;
        }
        EapduHandleFrame(frame, len, 0);
    }
    return 0;
}
