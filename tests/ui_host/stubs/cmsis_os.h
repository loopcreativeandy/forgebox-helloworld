#pragma once
#include <stdint.h>
typedef void *osThreadId_t; typedef void *osTimerId_t;
typedef enum { osPriorityNormal = 24, osPriorityHigh = 40 } osPriority_t;
typedef struct { const char *name; uint32_t stack_size; osPriority_t priority; } osThreadAttr_t;
typedef void (*osThreadFunc_t)(void *);
typedef void (*osTimerFunc_t)(void *);
typedef enum { osTimerOnce, osTimerPeriodic } osTimerType_t;
osThreadId_t osThreadNew(osThreadFunc_t f, void *a, const osThreadAttr_t *attr);
osTimerId_t osTimerNew(osTimerFunc_t f, osTimerType_t t, void *a, void *attr);
int osTimerStart(osTimerId_t t, uint32_t ticks);
uint32_t osKernelGetTickCount(void);
int osDelay(uint32_t ms);
int32_t osKernelLock(void);
int32_t osKernelUnlock(void);
