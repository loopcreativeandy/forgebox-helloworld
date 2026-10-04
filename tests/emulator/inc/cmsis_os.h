#include <stdint.h>
static inline void osDelay(uint32_t t){(void)t;}
static inline uint32_t osKernelGetTickCount(void){return 0;}
