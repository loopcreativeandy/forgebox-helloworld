#ifndef _SOL_TX_H
#define _SOL_TX_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define SOL_MAX_MESSAGE_LEN   1232U   /* Solana packet limit */
#define SOL_SUMMARY_MAX_LEN   640U

typedef enum {
    SOL_TX_OK = 0,
    SOL_TX_MALFORMED,
    SOL_TX_UNSUPPORTED_VERSION,
    SOL_TX_NOT_SIGNER,
} SolTxResult_t;

/* Parse a serialized Solana message (legacy or v0) and write a human summary for the screen.
 * signer = this device's pubkey; it must be one of the required signers. */
SolTxResult_t SolTxSummarize(const uint8_t *msg, size_t len, const uint8_t signer[32],
                             char *summary, size_t summarySize);

const char *SolTxResultText(SolTxResult_t r);

#endif
