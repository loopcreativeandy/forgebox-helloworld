#ifndef _SOL_KEY_H
#define _SOL_KEY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SOL_ADDRESS_MAX_LEN 45  /* 44 base58 chars + NUL */

/* BIP39 mnemonic (+ optional passphrase) -> 64-byte seed. No wordlist/checksum check. */
void Bip39MnemonicToSeed(const char *mnemonic, const char *passphrase, uint8_t seed[64]);

/* SLIP-10 ed25519 derivation; every index is hardened. Writes the 32-byte private key. */
void Slip10Ed25519Derive(const uint8_t *seed, size_t seedLen, const uint32_t *path, size_t depth, uint8_t key[32]);

/* Base58 (Bitcoin alphabet). Returns false if out is too small. */
bool Base58Encode(const uint8_t *data, size_t len, char *out, size_t outSize);

/* Wallet state: derive m/44'/501'/account'/0' from the mnemonic, keep the keypair in RAM. */
bool SolKeyLoad(const char *mnemonic, uint32_t account);
bool SolKeyReady(void);
const char *SolKeyAddress(void);
const uint8_t *SolKeyPubkey(void);
void SolKeySign(const uint8_t *msg, size_t len, uint8_t signature[64]);

#endif
