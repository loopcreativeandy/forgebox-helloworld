#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "eapdu.h"
#include "sol_key.h"
#include "test_mnemonic.h"
static uint8_t out[4096]; static int outn=0; static int pk=0;
void UsbSend(const uint8_t *d, uint32_t n){ memcpy(out+outn,d,n); outn+=n; pk++; printf("  pkt %d len %u: ",pk,n); for(uint32_t i=0;i<n;i++)printf("%02x",d[i]); printf("\n"); }
void UsbSetStatus(const char *f, ...){ va_list a; va_start(a,f); printf("  status: "); vprintf(f,a); printf("\n"); va_end(a);}
static void req(uint16_t cmd, const char *data){
  size_t len=strlen(data); int total=len?(len+54)/55:1;
  for(int i=0;i<total;i++){ uint8_t f[64]={0}; size_t c=len-i*55; if(c>55)c=55;
    f[0]=0; f[1]=cmd>>8; f[2]=cmd; f[3]=total>>8; f[4]=total; f[5]=i>>8; f[6]=i; f[7]=0x12; f[8]=0x34;
    memcpy(f+9,data+i*55,c); EapduHandleFrame(f,9+c,0);} }
int main(){ EapduInit(); SolKeyLoad(TEST_MNEMONIC,0);
 printf("echo short\n"); req(1,"hello forgebox"); 
 printf("echo 100B (2 pkts in, 2 out)\n"); req(1,"0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789");
 printf("info\n"); req(5,"");
 printf("unknown cmd 2\n"); req(2,"UR:SOL-SIGN-REQUEST/abc");
 printf("sol address (0x0100)\n"); req(0x100,"");
 return 0;}
