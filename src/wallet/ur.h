#ifndef _UR_H
#define _UR_H

/* Single-part UR (Blockchain Commons) with minimal bytewords + CRC32, and the two Solana
 * registry types the solana CLI's Keystone driver uses (ur-registry 1.0.8):
 *   sol-sign-request (1101): {1: #37 uuid, 2: sign-data, 3: #304 crypto-keypath, 4: address, 5: origin, 6: sign-type}
 *   sol-signature    (1102): {1: #37 uuid, 2: signature} */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define UR_TYPE_MAX         32U
#define UR_MAX_PATH_DEPTH   10U
#define UR_MAX_REQUEST_ID   32U

uint32_t Crc32(const uint8_t *data, size_t len);

/* "ur:<type>/<bytewords>" -> type + CBOR bytes. Multi-part URs are refused. */
bool UrDecode(const char *ur, size_t urLen, char type[UR_TYPE_MAX], uint8_t *out, size_t outSize, size_t *outLen);
bool UrEncode(const char *type, const uint8_t *data, size_t len, char *out, size_t outSize);

typedef struct {
    uint8_t requestId[UR_MAX_REQUEST_ID];
    size_t requestIdLen;            /* 0 = absent */
    const uint8_t *signData;        /* points into the CBOR buffer */
    size_t signDataLen;
    uint32_t path[UR_MAX_PATH_DEPTH];   /* raw indexes, bit 31 = hardened */
    size_t pathDepth;
    uint32_t sourceFingerprint;
    uint32_t signType;              /* 1 transaction, 2 message */
    char origin[32];
} SolSignRequest_t;

bool SolSignRequestParse(const uint8_t *cbor, size_t len, SolSignRequest_t *req);
/* CBOR-encode sol-signature; returns length or 0. */
size_t SolSignatureEncode(const SolSignRequest_t *req, const uint8_t sig[64], uint8_t *out, size_t outSize);

#endif
