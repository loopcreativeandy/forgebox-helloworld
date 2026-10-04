#include <stdio.h>
#include <string.h>
#include "sol_key.h"
#include "test_mnemonic.h"
#include "monocypher-ed25519.h"
int main(void){
  /* BIP39 vector (Trezor): "abandon x11 about", passphrase TREZOR */
  uint8_t s[64]; Bip39MnemonicToSeed("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about","TREZOR",s);
  printf("bip39 seed : "); for(int i=0;i<8;i++)printf("%02x",s[i]); printf("... (expect c55257c3)\n");
  /* SLIP-10 ed25519 test vector 1: seed 000102...0f, m/0' key 68e0fe46... */
  uint8_t seed[16]; for(int i=0;i<16;i++)seed[i]=i; uint32_t p[1]={0}; uint8_t k[32];
  Slip10Ed25519Derive(seed,16,p,1,k); printf("slip10 m/0': "); for(int i=0;i<4;i++)printf("%02x",k[i]); printf("... (expect 68e0fe46)\n");
  SolKeyLoad(TEST_MNEMONIC,0);
  printf("address    : %s\nexpected   : %s\n", SolKeyAddress(), TEST_MNEMONIC_EXPECTED_ADDRESS);
  uint8_t sig[64]; SolKeySign((const uint8_t*)"hi",2,sig);
  int ok = crypto_ed25519_check(sig, SolKeyPubkey(), (const uint8_t*)"hi",2)==0;
  printf("sign/verify: %s\n", ok?"OK":"FAIL");
  return strcmp(SolKeyAddress(),TEST_MNEMONIC_EXPECTED_ADDRESS)!=0 || !ok;
}
