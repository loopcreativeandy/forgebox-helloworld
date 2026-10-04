/* Slim EAPDU dispatcher. Replaces keystone3-firmware's eapdu_protocol_parser.c
 * (which runs framing inside an MPU sandbox and pulls in keystore/GUI/cJSON).
 * Framing itself is k3's eapdu_framing.c, unchanged. */
#include <stdio.h>
#include <string.h>
#include "cmsis_os.h"
#include "eapdu.h"
#include "eapdu_framing.h"
#include "usb_task.h"
#include "version.h"
#include "sol_key.h"
#include "sol_tx.h"
#include "approval.h"

#define RESPONSE_STATUS_LEN   2U
#define RESPONSE_DATA_MAX     (EAPDU_FRAMING_MAX_PACKET_SIZE - EAPDU_FRAMING_HEADER_SIZE - RESPONSE_STATUS_LEN)
#define ECHO_MAX_LEN          128U

/* ~11 KB: the reassembly buffer for up to 200 packets. Static, the protocol task owns it. */
static EapduFramingState_t g_framing;
static uint32_t g_requestCount = 0;

static void Put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFFU);
}

void EapduInit(void)
{
    EapduFramingReset(&g_framing);
}

void EapduSendResponse(uint16_t cmd, uint16_t requestId, uint16_t status, const uint8_t *data, uint32_t len)
{
    uint8_t packet[EAPDU_FRAMING_MAX_PACKET_SIZE];
    uint16_t total = (uint16_t)((len + RESPONSE_DATA_MAX - 1U) / RESPONSE_DATA_MAX);
    uint16_t index = 0;
    uint32_t offset = 0;

    if (total == 0U) {
        total = 1U;
    }
    do {
        uint32_t chunk = len - offset;
        if (chunk > RESPONSE_DATA_MAX) {
            chunk = RESPONSE_DATA_MAX;
        }
        packet[0] = EAPDU_CLA;
        Put16(packet + 1, cmd);
        Put16(packet + 3, total);
        Put16(packet + 5, index);
        Put16(packet + 7, requestId);
        if (chunk > 0U) {
            memcpy(packet + EAPDU_FRAMING_HEADER_SIZE, data + offset, chunk);
        }
        Put16(packet + EAPDU_FRAMING_HEADER_SIZE + chunk, status);
        UsbSend(packet, EAPDU_FRAMING_HEADER_SIZE + chunk + RESPONSE_STATUS_LEN);
        offset += chunk;
        index++;
        osDelay(10);    /* k3 paces packets the same way (UserDelay(10)) */
    } while (offset < len);
}

void EapduSendError(uint16_t cmd, uint16_t requestId, uint16_t status, const char *message)
{
    char json[160];
    int n = snprintf(json, sizeof(json), "{\"payload\":\"%s\"}", message != NULL ? message : "unknown error");
    if (n < 0) {
        n = 0;
    } else if ((uint32_t)n >= sizeof(json)) {
        n = sizeof(json) - 1;
    }
    EapduSendResponse(cmd, requestId, status, (const uint8_t *)json, (uint32_t)n);
}

static void EchoService(const EapduFramingResult_t *req)
{
    if (req->payload_length >= ECHO_MAX_LEN) {
        EapduSendError(CMD_ECHO_TEST, req->request_id, RSP_FAILURE_CODE, "echo payload too long");
        return;
    }
    EapduSendResponse(CMD_ECHO_TEST, req->request_id, RSP_SUCCESS_CODE, req->payload, req->payload_length);
}

static void GetDeviceInfoService(const EapduFramingResult_t *req)
{
    char version[32] = {0};
    char json[128];
    int n;

    GetUpdateVersionNumber(version);
    /* walletMFP: no seed loaded yet (step 2) → all zeros. */
    n = snprintf(json, sizeof(json), "{\"firmwareVersion\":\"%s\",\"walletMFP\":\"00000000\"}", version);
    EapduSendResponse(CMD_GET_DEVICE_INFO, req->request_id, RSP_SUCCESS_CODE, (const uint8_t *)json, (uint32_t)n);
}

static void GetSolAddressService(const EapduFramingResult_t *req)
{
    char json[128];
    int n;

    if (!SolKeyReady()) {
        EapduSendError(CMD_FB_GET_SOL_ADDRESS, req->request_id, RSP_FAILURE_CODE, "key not loaded");
        return;
    }
    n = snprintf(json, sizeof(json), "{\"address\":\"%s\",\"path\":\"m/44'/501'/0'/0'\"}", SolKeyAddress());
    EapduSendResponse(CMD_FB_GET_SOL_ADDRESS, req->request_id, RSP_SUCCESS_CODE, (const uint8_t *)json, (uint32_t)n);
}

static char g_summary[SOL_SUMMARY_MAX_LEN];

static void SignSolMessageService(const EapduFramingResult_t *req)
{
    SolTxResult_t parsed;
    ApprovalResult_t decision;
    uint8_t signature[64];
    char sigB58[100];
    char json[160];
    int n;

    if (!SolKeyReady()) {
        EapduSendError(CMD_FB_SIGN_SOL_MESSAGE, req->request_id, RSP_FAILURE_CODE, "key not loaded");
        return;
    }
    if (req->payload_length == 0U || req->payload_length > SOL_MAX_MESSAGE_LEN) {
        EapduSendError(CMD_FB_SIGN_SOL_MESSAGE, req->request_id, PRS_PARSING_ERROR, "message length out of range");
        return;
    }
    parsed = SolTxSummarize(req->payload, req->payload_length, SolKeyPubkey(), g_summary, sizeof(g_summary));
    if (parsed != SOL_TX_OK) {
        UsbSetStatus("Sign request refused: %s", SolTxResultText(parsed));
        EapduSendError(CMD_FB_SIGN_SOL_MESSAGE, req->request_id, PRS_PARSING_ERROR, SolTxResultText(parsed));
        return;
    }
    UsbSetStatus("Waiting for approval...");
    decision = ApprovalRequest(g_summary, APPROVAL_TIMEOUT_MS);
    if (decision != APPROVAL_APPROVED) {
        UsbSetStatus("Sign request %s", decision == APPROVAL_TIMEOUT ? "timed out" : "REJECTED");
        EapduSendError(CMD_FB_SIGN_SOL_MESSAGE, req->request_id, PRS_PARSING_REJECTED,
                       decision == APPROVAL_TIMEOUT ? "approval timed out" : "rejected on device");
        return;
    }
    SolKeySign(req->payload, req->payload_length, signature);
    if (!Base58Encode(signature, sizeof(signature), sigB58, sizeof(sigB58))) {
        EapduSendError(CMD_FB_SIGN_SOL_MESSAGE, req->request_id, RSP_FAILURE_CODE, "encode failed");
        return;
    }
    UsbSetStatus("SIGNED");
    n = snprintf(json, sizeof(json), "{\"signature\":\"%s\"}", sigB58);
    EapduSendResponse(CMD_FB_SIGN_SOL_MESSAGE, req->request_id, RSP_SUCCESS_CODE, (const uint8_t *)json, (uint32_t)n);
}

static const char *CommandName(uint16_t cmd)
{
    switch (cmd) {
    case CMD_ECHO_TEST:
        return "EchoTest";
    case CMD_RESOLVE_UR:
        return "ResolveUR";
    case CMD_CHECK_LOCK_STATUS:
        return "CheckLock";
    case CMD_EXPORT_ADDRESS:
        return "ExportAddress";
    case CMD_GET_DEVICE_INFO:
        return "GetDeviceInfo";
    case CMD_GET_DEVICE_USB_PUBKEY:
        return "GetUSBPubkey";
    case CMD_FB_GET_SOL_ADDRESS:
        return "GetSolAddress";
    case CMD_FB_SIGN_SOL_MESSAGE:
        return "SignSolMessage";
    default:
        return "unknown";
    }
}

static void Dispatch(const EapduFramingResult_t *req)
{
    g_requestCount++;
    UsbSetStatus("USB: #%lu %s (%lu B)", (unsigned long)g_requestCount,
                 CommandName(req->command_type), (unsigned long)req->payload_length);
    switch (req->command_type) {
    case CMD_ECHO_TEST:
        EchoService(req);
        break;
    case CMD_GET_DEVICE_INFO:
        GetDeviceInfoService(req);
        break;
    case CMD_FB_GET_SOL_ADDRESS:
        GetSolAddressService(req);
        break;
    case CMD_FB_SIGN_SOL_MESSAGE:
        SignSolMessageService(req);
        break;
    default:
        EapduSendError(req->command_type, req->request_id, PRS_PARSING_DISALLOWED, "not implemented on ForgeBox yet");
        break;
    }
}

void EapduHandleFrame(const uint8_t *frame, uint32_t len, uint32_t tick)
{
    EapduFramingResult_t res = EapduFramingPush(&g_framing, frame, len, tick);

    if (res.timed_out) {
        printf("EAPDU reassembly timeout\r\n");
    }
    switch (res.status) {
    case EAPDU_FRAMING_COMPLETE:
        Dispatch(&res);
        EapduFramingReset(&g_framing);
        break;
    case EAPDU_FRAMING_WAITING:
    case EAPDU_FRAMING_DUPLICATE:
        break;
    case EAPDU_FRAMING_ERROR_SHORT_FRAME:
        printf("EAPDU: short frame (%lu B)\r\n", (unsigned long)len);
        EapduFramingReset(&g_framing);
        break;
    case EAPDU_FRAMING_ERROR_TOTAL_PACKETS:
    case EAPDU_FRAMING_ERROR_TOTAL_MISMATCH:
        EapduSendError(res.command_type, res.request_id, PRS_INVALID_TOTAL_PACKETS, "Invalid total number of packets");
        EapduFramingReset(&g_framing);
        break;
    default:
        EapduSendError(res.command_type, res.request_id, PRS_INVALID_INDEX, "Invalid packet index/length");
        EapduFramingReset(&g_framing);
        break;
    }
}
