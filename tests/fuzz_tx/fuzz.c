/* Random + mutated messages through SolTxSummarize under ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sol_key.h"
#include "sol_tx.h"
#include "test_mnemonic.h"
int main(int argc, char **argv)
{
    /* a valid-ish legacy transfer to mutate: header + 3 keys + blockhash + 1 ix */
    uint8_t base[256]; size_t n = 0; char out[SOL_SUMMARY_MAX_LEN];
    long iters = argc > 1 ? atol(argv[1]) : 200000;
    SolKeyLoad(TEST_MNEMONIC, 0);
    base[n++] = 1; base[n++] = 0; base[n++] = 1; base[n++] = 3;
    memcpy(base + n, SolKeyPubkey(), 32); n += 32;
    memset(base + n, 7, 32); n += 32; memset(base + n, 0, 32); n += 32;   /* to, system program */
    memset(base + n, 9, 32); n += 32;                                   /* blockhash */
    base[n++] = 1; base[n++] = 2; base[n++] = 2; base[n++] = 0; base[n++] = 1; base[n++] = 12;
    base[n++] = 2; base[n++] = 0; base[n++] = 0; base[n++] = 0; memset(base + n, 0x11, 8); n += 8;
    if (SolTxSummarize(base, n, SolKeyPubkey(), out, sizeof(out)) != SOL_TX_OK) { printf("base not OK\n"); return 1; }
    printf("base summary:\n%s", out);
    srand(1);
    long ok = 0;
    for (long i = 0; i < iters; i++) {
        uint8_t m[256]; size_t len;
        if (i % 2) { len = rand() % 200; for (size_t k = 0; k < len; k++) m[k] = rand(); }
        else { len = n; memcpy(m, base, n); int flips = 1 + rand() % 4; while (flips--) m[rand() % n] = rand(); if (rand() % 4 == 0) len = rand() % n; }
        if (SolTxSummarize(m, len, SolKeyPubkey(), out, sizeof(out)) == SOL_TX_OK) ok++;
    }
    printf("%ld iterations, %ld parsed OK, no crash\n", iters, ok);
    return 0;
}
