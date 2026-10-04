#ifndef _EAPDU_H
#define _EAPDU_H

#include <stdint.h>

/* Keystone 3 "EAPDU" USB protocol, as spoken by the solana CLI (remote-wallet keystone.rs).
 * Frame: CLA(1) INS(2) P1=total packets(2) P2=packet index(2) LC=request id(2) data(<=55),
 * all big-endian. Response frames carry a trailing 2-byte status. */

#define EAPDU_CLA                 0x00U

#define CMD_ECHO_TEST             0x0001U
#define CMD_RESOLVE_UR            0x0002U
#define CMD_CHECK_LOCK_STATUS     0x0003U
#define CMD_EXPORT_ADDRESS        0x0004U
#define CMD_GET_DEVICE_INFO       0x0005U
#define CMD_GET_DEVICE_USB_PUBKEY 0x0006U
/* ForgeBox-only commands (not in keystone.rs), kept clear of Keystone's range. */
#define CMD_FB_GET_SOL_ADDRESS    0x0100U

#define RSP_SUCCESS_CODE          0x0000U
#define RSP_FAILURE_CODE          0x0001U
#define PRS_INVALID_TOTAL_PACKETS 0x0002U
#define PRS_INVALID_INDEX         0x0003U
#define PRS_PARSING_REJECTED      0x0004U
#define PRS_PARSING_ERROR         0x0005U
#define PRS_PARSING_DISALLOWED    0x0006U

void EapduInit(void);
void EapduHandleFrame(const uint8_t *frame, uint32_t len, uint32_t tick);
void EapduSendResponse(uint16_t cmd, uint16_t requestId, uint16_t status, const uint8_t *data, uint32_t len);
void EapduSendError(uint16_t cmd, uint16_t requestId, uint16_t status, const char *message);

#endif
