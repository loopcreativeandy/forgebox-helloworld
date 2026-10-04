/* Emulator approval: print the screen text to stderr, decide via FB_EMU_APPROVE (yes|no|ask, default yes). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "approval.h"

ApprovalResult_t ApprovalRequest(const char *text, uint32_t timeoutMs)
{
    const char *mode = getenv("FB_EMU_APPROVE");
    (void)timeoutMs;
    fprintf(stderr, "\n===== DEVICE SCREEN: Sign Solana transaction? =====\n%s===================================================\n", text);
    if (mode != NULL && strcmp(mode, "no") == 0) {
        fprintf(stderr, "[emulator] REJECT\n");
        return APPROVAL_REJECTED;
    }
    if (mode != NULL && strcmp(mode, "ask") == 0) {
        char line[16] = {0};
        FILE *tty = fopen("/dev/tty", "r");
        fprintf(stderr, "Approve? [y/N] ");
        if (tty != NULL && fgets(line, sizeof(line), tty) != NULL && (line[0] == 'y' || line[0] == 'Y')) {
            fclose(tty);
            return APPROVAL_APPROVED;
        }
        if (tty != NULL) {
            fclose(tty);
        }
        return APPROVAL_REJECTED;
    }
    fprintf(stderr, "[emulator] APPROVE\n");
    return APPROVAL_APPROVED;
}
