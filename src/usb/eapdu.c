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
#include "ur.h"
#include "monocypher.h"

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

/* ---- Stock solana CLI compatibility (agave remote-wallet keystone.rs) ---- */

#define SOLANA_COIN_TYPE  501U
#define UR_CBOR_MAX       (SOL_MAX_MESSAGE_LEN + 256U)

static uint8_t g_cbor[UR_CBOR_MAX];
static char g_urOut[512];
static char g_json[600];

/* 0x06: [coin u32 BE][depth u8][depth x u32 BE] -> {"pubkey": hex} */
static void GetUsbPubkeyService(const EapduFramingResult_t *req)
{
    const uint8_t *d = req->payload;
    uint32_t path[UR_MAX_PATH_DEPTH];
    uint8_t secret[64], pubkey[32];
    size_t depth, n;
    uint32_t coin;

    if (req->payload_length < 5) {
        EapduSendError(CMD_GET_DEVICE_USB_PUBKEY, req->request_id, PRS_PARSING_ERROR, "bad path");
        return;
    }
    coin = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
    depth = d[4];
    if (coin != SOLANA_COIN_TYPE || depth == 0 || depth > UR_MAX_PATH_DEPTH || req->payload_length != 5U + 4U * depth) {
        EapduSendError(CMD_GET_DEVICE_USB_PUBKEY, req->request_id, PRS_PARSING_ERROR, "bad path");
        return;
    }
    for (size_t i = 0; i < depth; i++) {
        const uint8_t *q = d + 5 + 4 * i;
        path[i] = ((uint32_t)q[0] << 24) | ((uint32_t)q[1] << 16) | ((uint32_t)q[2] << 8) | q[3];
    }
    if (!SolDeriveKeypair(path, depth, secret, pubkey)) {
        EapduSendError(CMD_GET_DEVICE_USB_PUBKEY, req->request_id, PRS_PARSING_ERROR, "path must be fully hardened");
        return;
    }
    crypto_wipe(secret, sizeof(secret));
    n = (size_t)snprintf(g_json, sizeof(g_json), "{\"pubkey\":\"");
    for (size_t i = 0; i < 32; i++) {
        n += (size_t)snprintf(g_json + n, sizeof(g_json) - n, "%02x", pubkey[i]);
    }
    n += (size_t)snprintf(g_json + n, sizeof(g_json) - n, "\"}");
    EapduSendResponse(CMD_GET_DEVICE_USB_PUBKEY, req->request_id, RSP_SUCCESS_CODE, (const uint8_t *)g_json, (uint32_t)n);
}

/* 0x02: "ur:sol-sign-request/..." -> approve -> {"payload": "ur:sol-signature/..."} */
static void ResolveUrService(const EapduFramingResult_t *req)
{
    char type[UR_TYPE_MAX];
    size_t cborLen, sigCborLen;
    SolSignRequest_t sr;
    uint8_t secret[64], pubkey[32], signature[64], sigCbor[128];
    char pathText[64];
    SolTxResult_t parsed;
    ApprovalResult_t decision;
    size_t used;
    int n;

    if (!UrDecode((const char *)req->payload, req->payload_length, type, g_cbor, sizeof(g_cbor), &cborLen)) {
        EapduSendError(CMD_RESOLVE_UR, req->request_id, PRS_PARSING_ERROR, "ur decode failed");
        return;
    }
    if (strcmp(type, "sol-sign-request") != 0) {
        UsbSetStatus("ResolveUR: unsupported %s", type);
        EapduSendError(CMD_RESOLVE_UR, req->request_id, PRS_PARSING_UNMATCHED, "unsupported UR type on ForgeBox");
        return;
    }
    if (!SolSignRequestParse(g_cbor, cborLen, &sr) || sr.signDataLen == 0 || sr.signDataLen > SOL_MAX_MESSAGE_LEN) {
        EapduSendError(CMD_RESOLVE_UR, req->request_id, PRS_PARSING_ERROR, "cbor decode failed");
        return;
    }
    if (!SolDeriveKeypair(sr.path, sr.pathDepth, secret, pubkey)) {
        EapduSendError(CMD_RESOLVE_UR, req->request_id, PRS_PARSING_ERROR, "path must be fully hardened");
        return;
    }
    parsed = SolTxSummarize(sr.signData, sr.signDataLen, pubkey, g_summary, sizeof(g_summary));
    if (parsed != SOL_TX_OK) {
        crypto_wipe(secret, sizeof(secret));
        UsbSetStatus("Sign request refused: %s", SolTxResultText(parsed));
        EapduSendError(CMD_RESOLVE_UR, req->request_id, PRS_PARSING_ERROR, SolTxResultText(parsed));
        return;
    }
    SolFormatPath(sr.path, sr.pathDepth, pathText, sizeof(pathText));
    used = strlen(g_summary);
    snprintf(g_summary + used, sizeof(g_summary) - used, "Key: %s\nFrom: %s\n", pathText,
             sr.origin[0] ? sr.origin : "unknown app");
    UsbSetStatus("Waiting for approval (solana CLI)...");
    decision = ApprovalRequest(g_summary, APPROVAL_TIMEOUT_MS);
    if (decision != APPROVAL_APPROVED) {
        crypto_wipe(secret, sizeof(secret));
        UsbSetStatus("Sign request %s", decision == APPROVAL_TIMEOUT ? "timed out" : "REJECTED");
        EapduSendError(CMD_RESOLVE_UR, req->request_id, PRS_PARSING_REJECTED,
                       decision == APPROVAL_TIMEOUT ? "approval timed out" : "rejected on device");
        return;
    }
    SolSignWithSecret(secret, sr.signData, sr.signDataLen, signature);
    crypto_wipe(secret, sizeof(secret));
    sigCborLen = SolSignatureEncode(&sr, signature, sigCbor, sizeof(sigCbor));
    if (sigCborLen == 0 || !UrEncode("sol-signature", sigCbor, sigCborLen, g_urOut, sizeof(g_urOut))) {
        EapduSendError(CMD_RESOLVE_UR, req->request_id, RSP_FAILURE_CODE, "encode failed");
        return;
    }
    UsbSetStatus("SIGNED (solana CLI)");
    n = snprintf(g_json, sizeof(g_json), "{\"payload\":\"%s\"}", g_urOut);
    EapduSendResponse(CMD_RESOLVE_UR, req->request_id, RSP_SUCCESS_CODE, (const uint8_t *)g_json, (uint32_t)n);
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
    case CMD_GET_DEVICE_USB_PUBKEY:
        GetUsbPubkeyService(req);
        break;
    case CMD_RESOLVE_UR:
        ResolveUrService(req);
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
