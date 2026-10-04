/* Solana key for the ForgeBox: BIP39 seed -> SLIP-10 m/44'/501'/a'/0' -> Ed25519 (Monocypher).
 * Same derivation as solana-keygen "prompt://?key=a/0", Phantom and Ledger. */
#include <string.h>
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "sol_key.h"

#define HARDENED        0x80000000UL
#define PBKDF2_ROUNDS   2048U

static uint8_t g_secretKey[64];     /* Monocypher layout: seed(32) || pubkey(32) */
static uint8_t g_pubkey[32];
static char g_address[SOL_ADDRESS_MAX_LEN];
static volatile bool g_ready = false;

void Bip39MnemonicToSeed(const char *mnemonic, const char *passphrase, uint8_t seed[64])
{
    /* PBKDF2-HMAC-SHA512, one 64-byte block: U1 = HMAC(P, S || INT(1)), Ui = HMAC(P, Ui-1). */
    crypto_sha512_hmac_ctx ctx;
    uint8_t u[64];
    const uint8_t blockIndex[4] = {0, 0, 0, 1};
    size_t mLen = strlen(mnemonic);

    if (passphrase == NULL) {
        passphrase = "";
    }
    crypto_sha512_hmac_init(&ctx, (const uint8_t *)mnemonic, mLen);
    crypto_sha512_hmac_update(&ctx, (const uint8_t *)"mnemonic", 8);
    crypto_sha512_hmac_update(&ctx, (const uint8_t *)passphrase, strlen(passphrase));
    crypto_sha512_hmac_update(&ctx, blockIndex, sizeof(blockIndex));
    crypto_sha512_hmac_final(&ctx, u);
    memcpy(seed, u, 64);
    for (uint32_t i = 1; i < PBKDF2_ROUNDS; i++) {
        crypto_sha512_hmac(u, (const uint8_t *)mnemonic, mLen, u, sizeof(u));
        for (uint32_t j = 0; j < 64; j++) {
            seed[j] ^= u[j];
        }
    }
    crypto_wipe(u, sizeof(u));
    crypto_wipe(&ctx, sizeof(ctx));
}

void Slip10Ed25519Derive(const uint8_t *seed, size_t seedLen, const uint32_t *path, size_t depth, uint8_t key[32])
{
    uint8_t i[64];
    uint8_t data[1 + 32 + 4];

    crypto_sha512_hmac(i, (const uint8_t *)"ed25519 seed", 12, seed, seedLen);
    for (size_t d = 0; d < depth; d++) {
        uint32_t index = path[d] | HARDENED;
        data[0] = 0x00;
        memcpy(data + 1, i, 32);                     /* parent key kL */
        data[33] = (uint8_t)(index >> 24);
        data[34] = (uint8_t)(index >> 16);
        data[35] = (uint8_t)(index >> 8);
        data[36] = (uint8_t)index;
        crypto_sha512_hmac(i, i + 32, 32, data, sizeof(data));   /* key = parent chain code */
    }
    memcpy(key, i, 32);
    crypto_wipe(i, sizeof(i));
    crypto_wipe(data, sizeof(data));
}

bool Base58Encode(const uint8_t *data, size_t len, char *out, size_t outSize)
{
    static const char alphabet[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint8_t digits[96] = {0};    /* base58 digits, little-endian; enough for 64 bytes in */
    size_t digitsLen = 1;
    size_t zeros = 0;
    size_t n = 0;

    if (len > 64) {
        return false;
    }
    while (zeros < len && data[zeros] == 0) {
        zeros++;
    }
    for (size_t k = zeros; k < len; k++) {
        uint32_t carry = data[k];
        for (size_t j = 0; j < digitsLen; j++) {
            carry += (uint32_t)digits[j] << 8;
            digits[j] = (uint8_t)(carry % 58);
            carry /= 58;
        }
        while (carry > 0) {
            digits[digitsLen++] = (uint8_t)(carry % 58);
            carry /= 58;
        }
    }
    if (zeros == len) {
        digitsLen = 0;
    }
    if (zeros + digitsLen + 1 > outSize) {
        return false;
    }
    for (size_t k = 0; k < zeros; k++) {
        out[n++] = '1';
    }
    for (size_t k = digitsLen; k > 0; k--) {
        out[n++] = alphabet[digits[k - 1]];
    }
    out[n] = '\0';
    return true;
}

bool SolKeyLoad(const char *mnemonic, uint32_t account)
{
    uint8_t seed[64];
    uint8_t key[32];
    const uint32_t path[4] = {44, 501, account, 0};

    g_ready = false;
    Bip39MnemonicToSeed(mnemonic, "", seed);
    Slip10Ed25519Derive(seed, sizeof(seed), path, 4, key);
    crypto_wipe(seed, sizeof(seed));
    crypto_ed25519_key_pair(g_secretKey, g_pubkey, key);    /* wipes key */
    if (!Base58Encode(g_pubkey, sizeof(g_pubkey), g_address, sizeof(g_address))) {
        return false;
    }
    g_ready = true;
    return true;
}

bool SolKeyReady(void)
{
    return g_ready;
}

const char *SolKeyAddress(void)
{
    return g_ready ? g_address : "";
}

const uint8_t *SolKeyPubkey(void)
{
    return g_pubkey;
}

void SolKeySign(const uint8_t *msg, size_t len, uint8_t signature[64])
{
    crypto_ed25519_sign(signature, g_secretKey, msg, len);
}
