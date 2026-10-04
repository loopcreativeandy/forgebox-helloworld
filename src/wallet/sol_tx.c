/* Minimal Solana message parser for the approval screen.
 * Knows SystemProgram Transfer, ComputeBudget and Memo; anything else is listed as
 * "program X, N bytes" so the user at least sees that something unknown is in there. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "monocypher-ed25519.h"
#include "sol_key.h"
#include "sol_tx.h"

#define MAX_KEYS 64U

typedef struct {
    const uint8_t *p;
    size_t left;
    bool bad;
} Reader_t;

static const uint8_t *Take(Reader_t *r, size_t n)
{
    const uint8_t *p = r->p;
    if (r->bad || n > r->left) {
        r->bad = true;
        return NULL;
    }
    r->p += n;
    r->left -= n;
    return p;
}

static uint8_t TakeU8(Reader_t *r)
{
    const uint8_t *p = Take(r, 1);
    return p != NULL ? *p : 0;
}

/* compact-u16 (shortvec) */
static uint16_t TakeShortVec(Reader_t *r)
{
    uint32_t value = 0;
    for (int shift = 0; shift <= 14; shift += 7) {
        uint8_t b = TakeU8(r);
        value |= (uint32_t)(b & 0x7FU) << shift;
        if ((b & 0x80U) == 0U) {
            return (uint16_t)value;
        }
    }
    r->bad = true;
    return 0;
}

static uint32_t Le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t Le64(const uint8_t *p)
{
    return (uint64_t)Le32(p) | ((uint64_t)Le32(p + 4) << 32);
}

static void Short(const uint8_t key[32], char *out, size_t outSize)
{
    char full[SOL_ADDRESS_MAX_LEN];
    if (!Base58Encode(key, 32, full, sizeof(full))) {
        snprintf(out, outSize, "?");
        return;
    }
    snprintf(out, outSize, "%s", full);
}

static void FormatSol(uint64_t lamports, char *out, size_t outSize)
{
    char frac[10];
    int n;
    snprintf(frac, sizeof(frac), "%09lu", (unsigned long)(lamports % 1000000000ULL));
    for (n = 8; n > 0 && frac[n] == '0'; n--) {
        frac[n] = '\0';
    }
    snprintf(out, outSize, "%lu.%s", (unsigned long)(lamports / 1000000000ULL), frac);
}

static bool KeyIsBase58(const uint8_t key[32], const char *b58)
{
    char s[SOL_ADDRESS_MAX_LEN];
    return Base58Encode(key, 32, s, sizeof(s)) && strcmp(s, b58) == 0;
}

static bool KeyIsZero(const uint8_t key[32])
{
    for (int i = 0; i < 32; i++) {
        if (key[i] != 0) {
            return false;
        }
    }
    return true;
}

typedef struct {
    char *buf;
    size_t size;
    size_t used;
} Out_t;

static void Append(Out_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void Append(Out_t *o, const char *fmt, ...)
{
    va_list ap;
    int n;
    if (o->used >= o->size) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(o->buf + o->used, o->size - o->used, fmt, ap);
    va_end(ap);
    if (n > 0) {
        o->used += (size_t)n;
        if (o->used >= o->size) {
            o->used = o->size - 1;
        }
    }
}

const char *SolTxResultText(SolTxResult_t r)
{
    switch (r) {
    case SOL_TX_OK:
        return "ok";
    case SOL_TX_MALFORMED:
        return "malformed message";
    case SOL_TX_UNSUPPORTED_VERSION:
        return "unsupported message version";
    case SOL_TX_NOT_SIGNER:
        return "this device's key is not a signer of this message";
    default:
        return "error";
    }
}

SolTxResult_t SolTxSummarize(const uint8_t *msg, size_t len, const uint8_t signer[32],
                             char *summary, size_t summarySize)
{
    Reader_t r = {msg, len, false};
    Out_t o = {summary, summarySize, 0};
    const uint8_t *keys;
    uint8_t numSigners, header1, header2;
    uint16_t numKeys, numInstr;
    bool versioned = false;
    int signerIndex = -1;
    uint8_t hash[64];
    char a[SOL_ADDRESS_MAX_LEN], b[SOL_ADDRESS_MAX_LEN], amount[24];

    summary[0] = '\0';
    if (len == 0U) {
        return SOL_TX_MALFORMED;
    }
    if ((msg[0] & 0x80U) != 0U) {
        if ((msg[0] & 0x7FU) != 0U) {
            return SOL_TX_UNSUPPORTED_VERSION;
        }
        versioned = true;
        (void)TakeU8(&r);
    }
    numSigners = TakeU8(&r);
    header1 = TakeU8(&r);
    header2 = TakeU8(&r);
    (void)header1;
    (void)header2;
    numKeys = TakeShortVec(&r);
    if (r.bad || numKeys == 0U || numKeys > MAX_KEYS || numSigners == 0U || numSigners > numKeys) {
        return SOL_TX_MALFORMED;
    }
    keys = Take(&r, (size_t)numKeys * 32U);
    (void)Take(&r, 32);     /* recent blockhash */
    if (r.bad) {
        return SOL_TX_MALFORMED;
    }
    for (int k = 0; k < numSigners; k++) {
        if (memcmp(keys + 32 * k, signer, 32) == 0) {
            signerIndex = k;
            break;
        }
    }
    if (signerIndex < 0) {
        return SOL_TX_NOT_SIGNER;
    }

    numInstr = TakeShortVec(&r);
    if (r.bad || numInstr == 0U) {
        return SOL_TX_MALFORMED;
    }
    for (uint16_t n = 0; n < numInstr; n++) {
        uint8_t prog = TakeU8(&r);
        uint16_t nAcc = TakeShortVec(&r);
        const uint8_t *acc = Take(&r, nAcc);
        uint16_t dLen = TakeShortVec(&r);
        const uint8_t *data = Take(&r, dLen);
        const uint8_t *progKey;

        if (r.bad || prog >= numKeys) {
            return SOL_TX_MALFORMED;
        }
        for (uint16_t k = 0; k < nAcc; k++) {
            if (acc[k] >= numKeys && !versioned) {
                return SOL_TX_MALFORMED;
            }
        }
        progKey = keys + 32 * prog;
        if (KeyIsZero(progKey) && dLen == 12 && Le32(data) == 2U && nAcc >= 2 &&
                acc[0] < numKeys && acc[1] < numKeys) {
            FormatSol(Le64(data + 4), amount, sizeof(amount));
            Short(keys + 32 * acc[0], a, sizeof(a));
            Short(keys + 32 * acc[1], b, sizeof(b));
            Append(&o, "SEND %s SOL\nto %s\nfrom %s%s\n\n", amount, b, a,
                   memcmp(keys + 32 * acc[0], signer, 32) == 0 ? " (this device)" : "");
        } else if (KeyIsBase58(progKey, "ComputeBudget111111111111111111111111111111")) {
            if (dLen == 5 && data[0] == 2U) {
                Append(&o, "Compute limit: %lu units\n", (unsigned long)Le32(data + 1));
            } else if (dLen == 9 && data[0] == 3U) {
                Append(&o, "Priority fee: %lu micro-lamports/CU\n", (unsigned long)Le64(data + 1));
            } else {
                Append(&o, "Compute budget setting\n");
            }
        } else if (KeyIsBase58(progKey, "MemoSq4gqABAXKb96qnH8TysNcJ9yDsX6kqQhUM3QFyt") ||
                   KeyIsBase58(progKey, "Memo1UhkJRfHyvLMcVucJwxXeuD728EqVDDwQDxFMNo")) {
            Append(&o, "Memo: \"");
            for (uint16_t k = 0; k < dLen && k < 80U; k++) {
                Append(&o, "%c", (data[k] >= 0x20 && data[k] < 0x7F) ? data[k] : '?');
            }
            Append(&o, "%s\"\n", dLen > 80U ? "..." : "");
        } else {
            Short(progKey, a, sizeof(a));
            Append(&o, "UNKNOWN program %s\n(%u accounts, %u bytes data)\n", a, nAcc, dLen);
        }
    }
    if (versioned) {
        uint16_t nLookups = TakeShortVec(&r);
        if (nLookups > 0U) {
            Append(&o, "Uses %u address lookup table(s)\n", nLookups);
        }
    }
    Short(keys, a, sizeof(a));
    Append(&o, "\nFee payer: %s\n", memcmp(keys, signer, 32) == 0 ? "this device" : a);
    crypto_sha512(hash, msg, len);
    Append(&o, "Msg fingerprint: %02x%02x-%02x%02x\n", hash[0], hash[1], hash[2], hash[3]);
    return SOL_TX_OK;
}
