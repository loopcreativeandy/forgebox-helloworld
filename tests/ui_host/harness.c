/* Host harness: the real UI task (src/tasks/helloworld_task.c) + real LVGL, fake LCD/touch/RTOS.
 * A finger touches the screen at t = TAP_START..TAP_END ms. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <execinfo.h>
#include "cmsis_os.h"
#include "mhscpu_gpio.h"
#include "hal_touch.h"
#include "lvgl.h"

static uint32_t g_tick;
static unsigned long g_px[3], g_flushes[3];
static osTimerFunc_t g_timerFn;
static int g_phase;           /* 0 init, 1 running, 2 touching */
static int g_tapX = 240, g_tapY = 400;
static uint32_t TAP_START = 3000, TAP_END = 3300, END = 6000;
int g_vendorRealloc = 0;      /* 1: mimic SramReallocTrack (malloc new, memcpy NEW size, free) */
static int g_approval;        /* 1: show the approval panel the whole time */

void HostAssert(const char *f, int l)
{
    void *bt[32];
    fprintf(stderr, "\n*** LV_ASSERT at %s:%d (phase %d, t=%u) — firmware would spin here -> watchdog\n", f, l, g_phase, g_tick);
    backtrace_symbols_fd(bt, backtrace(bt, 32), 2);
    exit(3);
}
void *HostMalloc(size_t n)
{
    void *p = malloc(n);
    if (n == 0 || p == NULL) fprintf(stderr, "malloc(%zu) -> %p phase %d (firmware: ASSERT p!=NULL)\n", n, p, g_phase);
    if (n == 0) { fprintf(stderr, "*** malloc(0): pvPortMalloc(0) returns NULL -> ASSERT -> hang\n"); exit(4); }
    return p;
}
void HostFree(void *p) { free(p); }
void *HostRealloc(void *p, size_t n)
{
    if (g_phase >= 1) fprintf(stderr, "realloc(%p, %zu) phase %d t=%u\n", p, n, g_phase, g_tick);
    if (n == 0) { fprintf(stderr, "*** realloc(p, 0): pvPortMalloc(0) -> NULL -> ASSERT -> hang\n"); exit(4); }
    if (p == NULL) fprintf(stderr, "    realloc with NULL: vendor memcpy(dest, NULL, %zu)\n", n);
    return realloc(p, n);
}

static osThreadFunc_t g_threadFn;
osThreadId_t osThreadNew(osThreadFunc_t f, void *a, const osThreadAttr_t *attr) { (void)attr; (void)a; g_threadFn = f; return (void *)1; }
osTimerId_t osTimerNew(osTimerFunc_t f, osTimerType_t t, void *a, void *attr) { (void)t; (void)a; (void)attr; g_timerFn = f; return (void *)1; }
int osTimerStart(osTimerId_t t, uint32_t ticks) { (void)t; (void)ticks; return 0; }
uint32_t osKernelGetTickCount(void) { return g_tick; }
int osDelay(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        g_tick++;
        if (g_timerFn && g_tick % 5 == 0) g_timerFn(NULL);
    }
    if (g_phase == 0 && g_tick > 100) g_phase = 1;
    if (g_tick >= TAP_START && g_tick < TAP_END) g_phase = 2; else if (g_phase == 2) g_phase = 1;
    if (g_tick >= END) {
        fprintf(stderr, "OK: reached t=%u without assert/crash\n", g_tick);
        fprintf(stderr, "init px %lu flushes %lu; lv_tick %u\n", g_px[0], g_flushes[0], lv_tick_get());
        fprintf(stderr, "pixels flushed: running %lu (%lu flushes over ~%u ms), touching %lu (%lu flushes over %u ms)\n",
                g_px[1], g_flushes[1], END - 100 - (TAP_END - TAP_START), g_px[2], g_flushes[2], TAP_END - TAP_START);
        exit(0);
    }
    return 0;
}

void NVIC_SystemReset(void) { fprintf(stderr, "*** NVIC_SystemReset\n"); exit(5); }
void WDT_ReloadCounter(void) {}
void GPIO_Init(void *port, GPIO_InitTypeDef *i) { (void)port; (void)i; }
int GPIO_ReadInputDataBit(void *port, uint32_t pin) { (void)port; (void)pin; return 1; }
void SYSCTRL_APBPeriphClockCmd(uint32_t p, int en) { (void)p; (void)en; }
bool LcdBusy(void) { return false; }
void LcdDraw(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint16_t *c) { (void)c; g_px[g_phase] += (unsigned long)(x2 - x1 + 1) * (y2 - y1 + 1); g_flushes[g_phase]++; }

void TouchInit(TouchPadIntCallbackFunc_t f) { (void)f; }
int32_t TouchGetStatus(TouchStatus_t *s)
{
    memset(s, 0, sizeof(*s));
    if (g_phase == 2) { s->touch = true; s->x = g_tapX; s->y = g_tapY; }
    return 0;
}

uint32_t UsbStatusSeq(void) { return 1; }
const char *UsbStatusText(void) { return "USB: test"; }
bool SolKeyReady(void) { return true; }
const char *SolKeyAddress(void) { return "Kti8hwMYH8iBLJNJhRqiWf8ttCcuHEw54o4FvD3RfLz"; }
bool ApprovalPending(uint32_t *seq) { if (seq) *seq = 1; return g_approval; }
const char *ApprovalText(void) { return "SEND 0.1 SOL\nto: CqPF...RzYF\n"; }
void ApprovalResolve(bool a) { fprintf(stderr, "ApprovalResolve(%d) t=%u\n", a, g_tick); g_approval = 0; }
void CrashUiStage(uint32_t s) { (void)s; }
const char *CrashLogReportText(void) { return ""; }

void CreateHelloWorldTask(void);

int main(int argc, char **argv)
{
    if (argc > 1) g_tapX = atoi(argv[1]);
    if (argc > 2) g_tapY = atoi(argv[2]);
    if (argc > 3) g_approval = atoi(argv[3]);
    setvbuf(stdout, NULL, _IONBF, 0);
    CreateHelloWorldTask();   /* registers the task and the LVGL tick timer */
    g_threadFn(NULL);
    return 1;
}

void ShowAssert(const char *file, uint32_t len) { HostAssert(file, (int)len); }

int32_t osKernelLock(void) { return 0; }
int32_t osKernelUnlock(void) { return 0; }
int GetHardwareVersion(void) { return 0; }
