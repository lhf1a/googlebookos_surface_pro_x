// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../probe/test2/sq2-ipa-acpi.h"
static void fix(unsigned char *p, unsigned int n, unsigned int field)
{
 unsigned int i; unsigned char sum=0;
 p[field]=0; for(i=0;i<n;i++)sum+=p[i]; p[field]=-sum;
}
int main(void)
{
 unsigned char p[64]={0}; unsigned long long xsdt=0;
 memcpy(p,"RSD PTR ",8);p[15]=2;p[20]=36;p[24]=0x80;p[27]=0xff;
 fix(p,20,8);fix(p,36,32);
 assert(!sq2_acpi_rsdp(p,36,&xsdt) && xsdt==0xff000080ULL);
 assert(sq2_acpi_rsdp(p,35,&xsdt));p[9]++;assert(sq2_acpi_rsdp(p,36,&xsdt));p[9]--;
 p[20]=64;assert(sq2_acpi_rsdp(p,36,&xsdt));
 memset(p,0,sizeof(p));memcpy(p,"XSDT",4);p[4]=44;
 assert(sq2_acpi_length(p,36,"XSDT",SQ2_ACPI_XSDT_MAX)==44);
 assert(!sq2_acpi_length(p,35,"XSDT",SQ2_ACPI_XSDT_MAX));
 assert(!sq2_acpi_length(p,36,"IORT",SQ2_ACPI_XSDT_MAX));
 p[4]=35;assert(!sq2_acpi_length(p,36,"XSDT",SQ2_ACPI_XSDT_MAX));
 p[4]=255;p[5]=255;p[6]=255;p[7]=255;
 assert(!sq2_acpi_length(p,36,"XSDT",SQ2_ACPI_XSDT_MAX));
 puts("ACPI_HELPERS_PASS: actual RSDP/checksum/length helpers, truncation/signature/overflow rejected");
 return 0;
}
