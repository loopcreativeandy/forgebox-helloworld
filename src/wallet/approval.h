#ifndef _APPROVAL_H
#define _APPROVAL_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    APPROVAL_APPROVED = 0,
    APPROVAL_REJECTED,
    APPROVAL_TIMEOUT,
} ApprovalResult_t;

#define APPROVAL_TIMEOUT_MS  60000U

/* Protocol task: show `text` and block until the user decides (or timeout). */
ApprovalResult_t ApprovalRequest(const char *text, uint32_t timeoutMs);

/* UI task: poll for a pending request, then resolve it. */
bool ApprovalPending(uint32_t *seq);
const char *ApprovalText(void);
void ApprovalResolve(bool approved);

#endif
