/* Mutated sol-sign-request CBOR + random UR strings under ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ur.h"
int main(int argc, char **argv)
{
    /* hand-built valid request: {1: #37 h'00'*16, 2: h'01020304', 3: #304 {1: [44,true,501,true], 2: 0}, 5: "solana cli", 6: 1} */
    uint8_t base[128]; size_t n = 0;
    uint8_t hdr[] = {0xA5, 0x01, 0xD8, 0x25, 0x50};
    memcpy(base, hdr, sizeof hdr); n += sizeof hdr; memset(base + n, 0, 16); n += 16;
    uint8_t rest[] = {0x02, 0x44, 1, 2, 3, 4, 0x03, 0xD9, 0x01, 0x30, 0xA2, 0x01, 0x84, 0x18, 0x2C, 0xF5, 0x19, 0x01, 0xF5, 0xF5, 0x02, 0x00,
                      0x05, 0x6A, 's','o','l','a','n','a',' ','c','l','i', 0x06, 0x01};
    memcpy(base + n, rest, sizeof rest); n += sizeof rest;
    SolSignRequest_t r; long iters = argc > 1 ? atol(argv[1]) : 200000, ok = 0, urok = 0;
    if (!SolSignRequestParse(base, n, &r) || r.pathDepth != 2 || r.path[1] != (501u | 0x80000000u) || r.signDataLen != 4) { printf("base parse FAILED\n"); return 1; }
    char ur[400]; UrEncode("sol-sign-request", base, n, ur, sizeof ur);
    printf("base ok: depth %zu origin '%s'\nUR %s\n", r.pathDepth, r.origin, ur);
    srand(2);
    for (long i = 0; i < iters; i++) {
        uint8_t m[128]; size_t len = n; memcpy(m, base, n);
        int flips = 1 + rand() % 4; while (flips--) m[rand() % n] = rand();
        if (rand() % 4 == 0) len = rand() % n;
        if (SolSignRequestParse(m, len, &r)) ok++;
        char s[400]; uint8_t out[200]; size_t ol; char t[UR_TYPE_MAX];
        size_t sl = strlen(ur); memcpy(s, ur, sl + 1);
        if (i % 2) { sl = rand() % 300; for (size_t k = 0; k < sl; k++) s[k] = "ur:/abcdefghijklmnopqrstuvwxyz-0123"[rand() % 35]; }
        else { s[rand() % sl] = 'a' + rand() % 26; }
        if (UrDecode(s, sl, t, out, sizeof out, &ol)) urok++;
    }
    printf("%ld iterations: %ld cbor parsed, %ld UR decoded, no crash\n", iters, ok, urok);
    return 0;
}
