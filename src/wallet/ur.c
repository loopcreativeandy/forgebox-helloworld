/* UR + CBOR, just enough for sol-sign-request / sol-signature. All parsing is bounds-checked:
 * the input arrives over USB from an untrusted host. */
#include <string.h>
#include <ctype.h>
#include "ur.h"
#include "bytewords_table.h"

uint32_t Crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

static int BytewordValue(char a, char b)
{
    a = (char)tolower((unsigned char)a);
    b = (char)tolower((unsigned char)b);
    for (int i = 0; i < 256; i++) {
        if (BYTEWORDS_MIN[i][0] == a && BYTEWORDS_MIN[i][1] == b) {
            return i;
        }
    }
    return -1;
}

bool UrDecode(const char *ur, size_t urLen, char type[UR_TYPE_MAX], uint8_t *out, size_t outSize, size_t *outLen)
{
    size_t pos = 3, typeLen = 0, n = 0, words;
    uint32_t crc;

    if (urLen < 4 || tolower((unsigned char)ur[0]) != 'u' || tolower((unsigned char)ur[1]) != 'r' || ur[2] != ':') {
        return false;
    }
    while (pos < urLen && ur[pos] != '/') {
        if (typeLen + 1 >= UR_TYPE_MAX) {
            return false;
        }
        type[typeLen++] = (char)tolower((unsigned char)ur[pos++]);
    }
    type[typeLen] = '\0';
    if (pos >= urLen || typeLen == 0) {
        return false;
    }
    pos++;  /* '/' */
    for (size_t k = pos; k < urLen; k++) {
        if (ur[k] == '/') {
            return false;   /* "seq-len/fragment": multi-part, not supported */
        }
    }
    if ((urLen - pos) % 2 != 0) {
        return false;
    }
    words = (urLen - pos) / 2;
    if (words < 4 || words - 4 > outSize) {
        return false;
    }
    for (size_t w = 0; w < words; w++) {
        int v = BytewordValue(ur[pos + 2 * w], ur[pos + 2 * w + 1]);
        uint8_t byte;
        if (v < 0) {
            return false;
        }
        byte = (uint8_t)v;
        if (w < words - 4) {
            out[n++] = byte;
        } else {
            /* checksum bytes, compared below */
            ((uint8_t *)&crc)[w - (words - 4)] = byte;
        }
    }
    {
        uint32_t expect = Crc32(out, n);
        uint8_t be[4] = {(uint8_t)(expect >> 24), (uint8_t)(expect >> 16), (uint8_t)(expect >> 8), (uint8_t)expect};
        if (memcmp(be, &crc, 4) != 0) {
            return false;
        }
    }
    *outLen = n;
    return true;
}

bool UrEncode(const char *type, const uint8_t *data, size_t len, char *out, size_t outSize)
{
    size_t typeLen = strlen(type);
    size_t need = 3 + typeLen + 1 + 2 * (len + 4) + 1;
    uint32_t crc = Crc32(data, len);
    uint8_t tail[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    char *p = out;

    if (need > outSize) {
        return false;
    }
    memcpy(p, "ur:", 3);
    p += 3;
    memcpy(p, type, typeLen);
    p += typeLen;
    *p++ = '/';
    for (size_t i = 0; i < len + 4; i++) {
        const char *w = BYTEWORDS_MIN[i < len ? data[i] : tail[i - len]];
        *p++ = w[0];
        *p++ = w[1];
    }
    *p = '\0';
    return true;
}

/* ---------------- minimal CBOR reader ---------------- */

typedef struct {
    const uint8_t *p;
    size_t left;
    bool bad;
} Cbor_t;

/* Reads an item head: major type + argument. */
static bool CborHead(Cbor_t *c, uint8_t *major, uint64_t *arg)
{
    uint8_t ib, info;
    size_t extra;
    if (c->bad || c->left < 1) {
        c->bad = true;
        return false;
    }
    ib = *c->p++;
    c->left--;
    *major = ib >> 5;
    info = ib & 0x1FU;
    if (info < 24) {
        *arg = info;
        return true;
    }
    if (info > 27) {
        c->bad = true;          /* indefinite lengths not used by ur-registry */
        return false;
    }
    extra = (size_t)1 << (info - 24);
    if (c->left < extra) {
        c->bad = true;
        return false;
    }
    *arg = 0;
    for (size_t i = 0; i < extra; i++) {
        *arg = (*arg << 8) | *c->p++;
    }
    c->left -= extra;
    return true;
}

static bool CborSkip(Cbor_t *c, int depth);

static bool CborSkipN(Cbor_t *c, uint64_t n, int depth)
{
    for (uint64_t i = 0; i < n; i++) {
        if (!CborSkip(c, depth)) {
            return false;
        }
    }
    return true;
}

static bool CborSkip(Cbor_t *c, int depth)
{
    uint8_t major;
    uint64_t arg;
    if (depth > 8 || !CborHead(c, &major, &arg)) {
        c->bad = true;
        return false;
    }
    switch (major) {
    case 0: case 1: case 7:
        return true;
    case 2: case 3:
        if (arg > c->left) {
            c->bad = true;
            return false;
        }
        c->p += arg;
        c->left -= (size_t)arg;
        return true;
    case 4:
        return arg <= c->left && CborSkipN(c, arg, depth + 1);
    case 5:
        return arg <= c->left && CborSkipN(c, arg * 2, depth + 1);
    case 6:
        return CborSkip(c, depth + 1);
    default:
        c->bad = true;
        return false;
    }
}

static bool CborBytes(Cbor_t *c, uint8_t wantMajor, const uint8_t **data, size_t *len)
{
    uint8_t major;
    uint64_t arg;
    if (!CborHead(c, &major, &arg) || major != wantMajor || arg > c->left) {
        c->bad = true;
        return false;
    }
    *data = c->p;
    *len = (size_t)arg;
    c->p += arg;
    c->left -= (size_t)arg;
    return true;
}

static bool CborUint(Cbor_t *c, uint64_t *v)
{
    uint8_t major;
    if (!CborHead(c, &major, v) || major != 0) {
        c->bad = true;
        return false;
    }
    return true;
}

static bool CborExpectTag(Cbor_t *c, uint64_t tag)
{
    uint8_t major;
    uint64_t arg;
    if (!CborHead(c, &major, &arg) || major != 6 || arg != tag) {
        c->bad = true;
        return false;
    }
    return true;
}

static bool ParseKeyPath(Cbor_t *c, SolSignRequest_t *req)
{
    uint8_t major;
    uint64_t n, key;
    if (!CborExpectTag(c, 304) || !CborHead(c, &major, &n) || major != 5 || n > 8) {
        return false;
    }
    for (uint64_t i = 0; i < n; i++) {
        if (!CborUint(c, &key)) {
            return false;
        }
        if (key == 1) {             /* components: [index, hardened, index, hardened, ...] */
            uint64_t items;
            if (!CborHead(c, &major, &items) || major != 4 || items % 2 != 0 || items / 2 > UR_MAX_PATH_DEPTH) {
                return false;
            }
            req->pathDepth = (size_t)(items / 2);
            for (size_t k = 0; k < req->pathDepth; k++) {
                uint64_t index, hardened;
                if (!CborUint(c, &index) || index > 0x7FFFFFFFULL) {
                    return false;   /* wildcards ([]) are not allowed in a signing path */
                }
                if (!CborHead(c, &major, &hardened) || major != 7 || (hardened != 20 && hardened != 21)) {
                    return false;
                }
                req->path[k] = (uint32_t)index | (hardened == 21 ? 0x80000000UL : 0U);
            }
        } else if (key == 2) {
            uint64_t fp;
            if (!CborUint(c, &fp) || fp > 0xFFFFFFFFULL) {
                return false;
            }
            req->sourceFingerprint = (uint32_t)fp;
        } else if (!CborSkip(c, 0)) {
            return false;
        }
    }
    return !c->bad;
}

bool SolSignRequestParse(const uint8_t *cbor, size_t len, SolSignRequest_t *req)
{
    Cbor_t c = {cbor, len, false};
    uint8_t major;
    uint64_t n, key;
    bool havePath = false;

    memset(req, 0, sizeof(*req));
    req->signType = 1;
    if (!CborHead(&c, &major, &n) || major != 5 || n > 16) {
        return false;
    }
    for (uint64_t i = 0; i < n; i++) {
        const uint8_t *d;
        size_t dl;
        if (!CborUint(&c, &key)) {
            return false;
        }
        switch (key) {
        case 1:
            if (!CborExpectTag(&c, 37) || !CborBytes(&c, 2, &d, &dl) || dl > UR_MAX_REQUEST_ID) {
                return false;
            }
            memcpy(req->requestId, d, dl);
            req->requestIdLen = dl;
            break;
        case 2:
            if (!CborBytes(&c, 2, &req->signData, &req->signDataLen)) {
                return false;
            }
            break;
        case 3:
            if (!ParseKeyPath(&c, req)) {
                return false;
            }
            havePath = true;
            break;
        case 5:
            if (!CborBytes(&c, 3, &d, &dl)) {
                return false;
            }
            dl = dl < sizeof(req->origin) - 1 ? dl : sizeof(req->origin) - 1;
            memcpy(req->origin, d, dl);
            req->origin[dl] = '\0';
            break;
        case 6: {
            uint64_t t;
            if (!CborUint(&c, &t) || t > 0xFFFF) {
                return false;
            }
            req->signType = (uint32_t)t;
            break;
        }
        default:
            if (!CborSkip(&c, 0)) {
                return false;
            }
            break;
        }
    }
    return !c.bad && c.left == 0 && havePath && req->signData != NULL;
}

size_t SolSignatureEncode(const SolSignRequest_t *req, const uint8_t sig[64], uint8_t *out, size_t outSize)
{
    size_t n = 0;
    size_t need = 1 + (req->requestIdLen ? 1 + 2 + 2 + req->requestIdLen : 0) + 1 + 2 + 64;
    if (need > outSize) {
        return 0;
    }
    out[n++] = req->requestIdLen ? 0xA2 : 0xA1;            /* map(2|1) */
    if (req->requestIdLen) {
        out[n++] = 0x01;
        out[n++] = 0xD8;                                    /* tag(37) uuid */
        out[n++] = 0x25;
        if (req->requestIdLen < 24) {
            out[n++] = (uint8_t)(0x40 | req->requestIdLen);
        } else {
            out[n++] = 0x58;
            out[n++] = (uint8_t)req->requestIdLen;
        }
        memcpy(out + n, req->requestId, req->requestIdLen);
        n += req->requestIdLen;
    }
    out[n++] = 0x02;
    out[n++] = 0x58;                                        /* bytes(64) */
    out[n++] = 0x40;
    memcpy(out + n, sig, 64);
    n += 64;
    return n;
}
