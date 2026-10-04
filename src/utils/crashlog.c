#include <stdio.h>
#include <string.h>
#include "crashlog.h"

#define CRASH_MAGIC       0xC4A5B10CUL
#define STAGE_MAGIC       0x57A60000UL

#define KIND_FAULT        1U
#define KIND_ASSERT       2U
#define KIND_HANG         3U       /* watchdog interrupt (NMI): first timeout, before the reset */

/* Cortex-M4 System Control Block registers */
#define REG(a)            (*(volatile uint32_t *)(a))
#define SCB_AIRCR         REG(0xE000ED0CUL)
#define SCB_CFSR          REG(0xE000ED28UL)
#define SCB_HFSR          REG(0xE000ED2CUL)
#define SCB_MMFAR         REG(0xE000ED34UL)
#define SCB_BFAR          REG(0xE000ED38UL)

typedef struct {
    uint32_t magic;
    uint32_t kind;
    uint32_t stage;           /* breadcrumb at the time of the crash */
    uint32_t pc, lr, psr, sp, excReturn;
    uint32_t cfsr, hfsr, mmfar, bfar;
    const char *file;         /* assert: points into flash, same image after reset */
    uint32_t line;
    uint32_t uiStage;
    char task[12];
    uint32_t check;
} CrashLog_t;

/* NOLOAD section above .bss (see mh1903b.ld): startup code neither loads nor zeroes it. */
static volatile CrashLog_t g_crash __attribute__((section(".noinit_crash")));
static volatile uint32_t g_stage __attribute__((section(".noinit_crash")));
static volatile uint32_t g_uiStage __attribute__((section(".noinit_crash")));

extern char *pcTaskGetName(void *xTaskToQuery);

static uint32_t Check(const volatile CrashLog_t *c)
{
    const volatile uint32_t *w = (const volatile uint32_t *)c;
    uint32_t sum = 0x1234567UL;
    for (size_t i = 0; i < offsetof(CrashLog_t, check) / 4U; i++) {
        sum = (sum << 5) ^ (sum >> 27) ^ w[i];
    }
    return sum;
}

static void __attribute__((noreturn)) ResetNow(void)
{
    __asm volatile ("dsb 0xF" ::: "memory");
    SCB_AIRCR = 0x05FA0004UL;            /* SYSRESETREQ */
    __asm volatile ("dsb 0xF" ::: "memory");
    for (;;) {
    }
}

void CrashStage(uint32_t stage)
{
    g_stage = STAGE_MAGIC | (stage & 0xFFFFU);
}

void CrashUiStage(uint32_t stage)
{
    g_uiStage = STAGE_MAGIC | (stage & 0xFFFFU);
}

static uint32_t CurrentStage(void)
{
    return ((g_stage & 0xFFFF0000UL) == STAGE_MAGIC) ? (g_stage & 0xFFFFU) : 0xFFFFU;
}

static uint32_t CurrentUiStage(void)
{
    return ((g_uiStage & 0xFFFF0000UL) == STAGE_MAGIC) ? (g_uiStage & 0xFFFFU) : 0xFFFFU;
}

static void RecordTask(void)
{
    const char *name = pcTaskGetName(NULL);
    if ((uint32_t)name >= 0x20000000UL && (uint32_t)name < 0x20100000UL) {
        for (size_t i = 0; i < sizeof(g_crash.task) - 1U && name[i] != '\0'; i++) {
            g_crash.task[i] = name[i];
        }
    }
}

void CrashAssert(const char *file, uint32_t line)
{
    __asm volatile ("cpsid i" ::: "memory");
    memset((void *)&g_crash, 0, sizeof(g_crash));
    g_crash.kind = KIND_ASSERT;
    g_crash.stage = CurrentStage();
    g_crash.uiStage = CurrentUiStage();
    RecordTask();
    g_crash.file = file;
    g_crash.line = line;
    __asm volatile ("mov %0, lr" : "=r" (g_crash.lr));
    g_crash.magic = CRASH_MAGIC;
    g_crash.check = Check(&g_crash);
    ResetNow();
}

static void __attribute__((noreturn)) RecordFrame(uint32_t kind, const uint32_t *frame, uint32_t excReturn)
{
    memset((void *)&g_crash, 0, sizeof(g_crash));
    g_crash.kind = kind;
    g_crash.stage = CurrentStage();
    g_crash.uiStage = CurrentUiStage();
    RecordTask();
    g_crash.excReturn = excReturn;
    g_crash.sp = (uint32_t)frame;
    g_crash.cfsr = SCB_CFSR;
    g_crash.hfsr = SCB_HFSR;
    g_crash.mmfar = SCB_MMFAR;
    g_crash.bfar = SCB_BFAR;
    /* A bad stack pointer would fault again here; only read the frame if it is in SRAM. */
    if ((uint32_t)frame >= 0x20000000UL && (uint32_t)frame < 0x20100000UL - 32U) {
        g_crash.lr = frame[5];
        g_crash.pc = frame[6];
        g_crash.psr = frame[7];
    }
    g_crash.magic = CRASH_MAGIC;
    g_crash.check = Check(&g_crash);
    ResetNow();
}

void CrashFaultC(const uint32_t *frame, uint32_t excReturn)
{
    RecordFrame(KIND_FAULT, frame, excReturn);
}

void CrashHangC(const uint32_t *frame, uint32_t excReturn)
{
    RecordFrame(KIND_HANG, frame, excReturn);
}

/* Overrides the weak aliases to Default_Handler (an endless loop) in startup_mhscpu.s. */
#define FAULT_ENTRY(name, target)                           \
    void __attribute__((naked)) name(void)                  \
    {                                                       \
        __asm volatile (                                    \
            "tst lr, #4      \n"                            \
            "ite eq          \n"                            \
            "mrseq r0, msp   \n"                            \
            "mrsne r0, psp   \n"                            \
            "mov r1, lr      \n"                            \
            "b " #target "   \n");                          \
    }

FAULT_ENTRY(HardFault_Handler, CrashFaultC)
FAULT_ENTRY(MemManage_Handler, CrashFaultC)
FAULT_ENTRY(BusFault_Handler, CrashFaultC)
FAULT_ENTRY(UsageFault_Handler, CrashFaultC)
/* The watchdog runs in interrupt mode (main.c). On MegaHunt parts its interrupt is
 * expected on NMI; if it is not, the second timeout still resets the chip. */
FAULT_ENTRY(NMI_Handler, CrashHangC)

static char g_report[200];

const char *CrashLogReportText(void)
{
    return g_report;
}

void CrashLogBootReport(void)
{
    char *out = g_report;
    size_t outSize = sizeof(g_report);
    uint32_t stage = CurrentStage();

    out[0] = '\0';
    if (g_crash.magic == CRASH_MAGIC && g_crash.check == Check(&g_crash)) {
        if (g_crash.kind == KIND_FAULT || g_crash.kind == KIND_HANG) {
            (void)snprintf(out, outSize,
                                 "LAST CRASH: %s in %s, stage %lu ui %lu\npc=%08lx lr=%08lx\ncfsr=%08lx hfsr=%08lx\nbfar=%08lx mmfar=%08lx",
                                 g_crash.kind == KIND_HANG ? "HANG (watchdog)" : "fault", (const char *)g_crash.task,
                                 (unsigned long)g_crash.stage, (unsigned long)g_crash.uiStage,
                                 (unsigned long)g_crash.pc, (unsigned long)g_crash.lr,
                                 (unsigned long)g_crash.cfsr, (unsigned long)g_crash.hfsr,
                                 (unsigned long)g_crash.bfar, (unsigned long)g_crash.mmfar);
        } else {
            const char *file = g_crash.file;
            const char *base = file;
            /* the string lives in flash; guard against a garbage pointer anyway */
            if ((uint32_t)file >= 0x01000000UL && (uint32_t)file < 0x02000000UL) {
                for (const char *p = file; *p != '\0' && p - file < 200; p++) {
                    if (*p == '/' || *p == '\\') {
                        base = p + 1;
                    }
                }
            } else {
                base = "?";
            }
            (void)snprintf(out, outSize, "LAST CRASH: assert in %s, stage %lu ui %lu\n%s:%lu\nlr=%08lx",
                                 (const char *)g_crash.task, (unsigned long)g_crash.stage, (unsigned long)g_crash.uiStage,
                                 base, (unsigned long)g_crash.line,
                                 (unsigned long)g_crash.lr);
        }
    } else if ((stage != 0U && stage != 0xFFFFU) || (CurrentUiStage() != 0xFFFFU && CurrentUiStage() >= 6U)) {
        (void)snprintf(out, outSize, "LAST RESET during stage %lu ui %lu\n(no record: watchdog reset without NMI?)",
                             (unsigned long)stage, (unsigned long)CurrentUiStage());
    }
    memset((void *)&g_crash, 0, sizeof(g_crash));
    g_stage = STAGE_MAGIC;              /* stage 0 = idle */
    g_uiStage = STAGE_MAGIC;
    if (out[0] != '\0') {
        printf("%s\r\n", out);
    }
}
