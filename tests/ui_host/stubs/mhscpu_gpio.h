#pragma once
#include <stdint.h>
typedef struct { uint32_t GPIO_Pin, GPIO_Mode, GPIO_Remap; } GPIO_InitTypeDef;
#define GPIOE ((void*)0) 
#define GPIO_Pin_14 (1u<<14)
#define GPIO_Mode_IPU 1
#define GPIO_Remap_1 1
#define Bit_RESET 0
#define SYSCTRL_APBPeriph_GPIO 1
#define ENABLE 1
void GPIO_Init(void *port, GPIO_InitTypeDef *i);
int GPIO_ReadInputDataBit(void *port, uint32_t pin);
void SYSCTRL_APBPeriphClockCmd(uint32_t p, int en);
