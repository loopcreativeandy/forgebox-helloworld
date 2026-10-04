/* ForgeBox USB plumbing, modelled on keystone3-firmware src/tasks/usb_task.c +
 * data_parser_task.c, without its message bus / sandbox / UI dependencies.
 *
 *   USB IRQ --notify--> UsbTask: USBD_OTG_ISR_Handler(), re-enable IRQ, pump TX
 *   cdc DataOut --queue--> ProtocolTask: EapduHandleFrame()
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "drv_usb.h"
#include "usbd_cdc_core.h"
#include "usb_dcd_int.h"
#include "usb_task.h"
#include "eapdu.h"

#define RX_QUEUE_DEPTH  32U

typedef struct {
    uint8_t len;
    uint8_t data[USB_FRAME_SIZE];
} UsbFrame_t;

extern USB_OTG_CORE_HANDLE g_usbDev;

static TaskHandle_t g_usbTask = NULL;
static QueueHandle_t g_rxQueue = NULL;
static char g_status[96] = "USB: waiting for host";
static volatile uint32_t g_statusSeq = 1;

static void UsbTask(void *argument);
static void ProtocolTask(void *argument);

void CreateUsbTask(void)
{
    const osThreadAttr_t usbAttr = {
        .name = "UsbTask",
        .priority = osPriorityHigh,
        .stack_size = 1024 * 4,
    };
    const osThreadAttr_t protoAttr = {
        .name = "ProtocolTask",
        .priority = osPriorityNormal,
        .stack_size = 1024 * 8,
    };
    g_rxQueue = xQueueCreate(RX_QUEUE_DEPTH, sizeof(UsbFrame_t));
    osThreadNew(UsbTask, NULL, &usbAttr);
    osThreadNew(ProtocolTask, NULL, &protoAttr);
}

void UsbIsrNotify(void)
{
    BaseType_t woken = pdFALSE;
    if (g_usbTask == NULL) {
        /* IRQ masked by the caller; the task re-enables it once it runs. */
        return;
    }
    vTaskNotifyGiveFromISR(g_usbTask, &woken);
    portYIELD_FROM_ISR(woken);
}

static void UsbTask(void *argument)
{
    (void)argument;
    g_usbTask = xTaskGetCurrentTaskHandle();
    UsbInit();
    UsbSetIRQ(true);
    while (1) {
        /* Timeout as a safety net: never leave the IRQ masked for long. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100));
        USBD_OTG_ISR_Handler(&g_usbDev);
        NVIC_ClearPendingIRQ(USB_IRQn);
        UsbSetIRQ(true);
        USBD_cdc_TxPump();
    }
}

bool UsbRxPushFrame(const uint8_t *data, uint32_t len)
{
    UsbFrame_t frame;
    if (g_rxQueue == NULL || data == NULL || len == 0U || len > USB_FRAME_SIZE) {
        return false;
    }
    frame.len = (uint8_t)len;
    memcpy(frame.data, data, len);
    return xQueueSend(g_rxQueue, &frame, 0) == pdTRUE;
}

void UsbSend(const uint8_t *data, uint32_t len)
{
    USBD_cdc_SendBuffer_Cb(data, len);
}

static void ProtocolTask(void *argument)
{
    UsbFrame_t frame;
    (void)argument;
    EapduInit();
    while (1) {
        if (xQueueReceive(g_rxQueue, &frame, portMAX_DELAY) == pdTRUE) {
            EapduHandleFrame(frame.data, frame.len, osKernelGetTickCount());
        }
    }
}

void UsbSetStatus(const char *fmt, ...)
{
    char text[sizeof(g_status)];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    vTaskSuspendAll();
    memcpy(g_status, text, sizeof(g_status));
    g_statusSeq++;
    (void)xTaskResumeAll();
    printf("%s\r\n", text);
}

const char *UsbStatusText(void)
{
    return g_status;
}

uint32_t UsbStatusSeq(void)
{
    return g_statusSeq;
}
