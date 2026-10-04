/* Cross-task approval: ProtocolTask asks, the UI task (snake/helloworld) shows and answers. */
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "approval.h"
#include "sol_tx.h"

static SemaphoreHandle_t g_done = NULL;
static char g_text[SOL_SUMMARY_MAX_LEN];
static volatile bool g_pending = false;
static volatile bool g_approved = false;
static volatile uint32_t g_seq = 0;

ApprovalResult_t ApprovalRequest(const char *text, uint32_t timeoutMs)
{
    if (g_done == NULL) {
        g_done = xSemaphoreCreateBinary();
    }
    (void)xSemaphoreTake(g_done, 0);            /* drop a stale give */
    strncpy(g_text, text, sizeof(g_text) - 1U);
    g_text[sizeof(g_text) - 1U] = '\0';
    g_approved = false;
    g_seq++;
    g_pending = true;
    if (xSemaphoreTake(g_done, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
        g_pending = false;                       /* UI notices and closes the panel */
        return APPROVAL_TIMEOUT;
    }
    return g_approved ? APPROVAL_APPROVED : APPROVAL_REJECTED;
}

bool ApprovalPending(uint32_t *seq)
{
    if (seq != NULL) {
        *seq = g_seq;
    }
    return g_pending;
}

const char *ApprovalText(void)
{
    return g_text;
}

void ApprovalResolve(bool approved)
{
    if (!g_pending) {
        return;
    }
    g_approved = approved;
    g_pending = false;
    if (g_done != NULL) {
        (void)xSemaphoreGive(g_done);
    }
}
