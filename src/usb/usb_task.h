#ifndef _USB_TASK_H
#define _USB_TASK_H

#include <stdint.h>
#include <stdbool.h>

#define USB_FRAME_SIZE 64U

void CreateUsbTask(void);

/* USB IRQ: mask the IRQ and wake the USB task (k3 does the same via its msg bus). */
void UsbIsrNotify(void);

/* Called from the cdc core's DataOut (USB task context): one 64-byte EAPDU frame. */
bool UsbRxPushFrame(const uint8_t *data, uint32_t len);

/* Queue bytes on the IN endpoint (split into 64-byte packets by the cdc core). */
void UsbSend(const uint8_t *data, uint32_t len);

/* Short human-readable status for the screen; updated by the protocol task. */
const char *UsbStatusText(void);
uint32_t UsbStatusSeq(void);
void UsbSetStatus(const char *fmt, ...);

#endif
