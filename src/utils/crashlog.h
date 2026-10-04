/* Crash catcher: CPU faults and asserts are recorded in RAM that survives a reset,
 * the device resets right away, and the next boot shows the record on screen.
 * Breadcrumbs (CrashStage) say which step of a request was running at the time;
 * a breadcrumb with no fault/assert record means a hang that the watchdog reset. */
#ifndef _CRASHLOG_H
#define _CRASHLOG_H

#include <stdint.h>
#include <stddef.h>

void CrashStage(uint32_t stage);
/* UI-task breadcrumb (the UI task feeds the watchdog, so a hang there = watchdog reset). */
void CrashUiStage(uint32_t stage);
void CrashAssert(const char *file, uint32_t line);
/* Call once at boot, before the scheduler: formats the previous record (if any) and clears it. */
void CrashLogBootReport(void);
/* "" when the last reset was clean. */
const char *CrashLogReportText(void);

#endif
