/* ForgeBox: no-op VCP interface, see usbd_cdc_vcp.h. Data goes through usb_task.c. */
#include "usbd_cdc_vcp.h"

static uint16_t VcpInit(void)
{
    return USBD_OK;
}

static uint16_t VcpDeInit(void)
{
    return USBD_OK;
}

static uint16_t VcpCtrl(uint32_t cmd, uint8_t *buf, uint32_t len)
{
    (void)cmd;
    (void)buf;
    (void)len;
    return USBD_OK;
}

static uint16_t VcpDataTx(uint8_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
    return USBD_OK;
}

static uint16_t VcpDataRx(uint8_t *buf, uint32_t len)
{
    (void)buf;
    (void)len;
    return USBD_OK;
}

CDC_IF_Prop_TypeDef VCPHandle = {
    VcpInit,
    VcpDeInit,
    VcpCtrl,
    VcpDataTx,
    VcpDataRx,
};
