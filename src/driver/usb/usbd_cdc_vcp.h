/* ForgeBox: the USB interface is a vendor (WinUSB) bulk pipe, not a serial port.
 * The cdc core still wants an APP_FOPS handle, so this is a no-op stub. */
#ifndef __USBD_CDC_VCP_H
#define __USBD_CDC_VCP_H

#include "usbd_cdc_core.h"

extern CDC_IF_Prop_TypeDef VCPHandle;

#endif /* __USBD_CDC_VCP_H */
