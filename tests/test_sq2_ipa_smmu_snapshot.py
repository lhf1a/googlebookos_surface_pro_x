"""Run the actual IPA GR0 scan against bounded memory, including extra groups."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'probe/test2/sq2-ipa-smmu-snapshot.c').read_text()
scan=s[s.index('\tid0 = readl('):s.index('\nunmap:')]
h=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
typedef uint32_t u32;
#define BIT(n) (1U<<(n))
static u32 page[1024], reads, id0, scr0, groups, matches[3];
static u32 readl(void *p) {
 uintptr_t n=(unsigned char *)p-(unsigned char *)page;
 assert(!(n&3) && (n==0 || n==0x20 || (n>=0x800 && n<0xffc)));
 reads++; return page[n/4];
}
#define pr_info(...) ((void)0)
static int run(void) {
 void *gr0=page;
 u32 i,j,smr,s2cr,mask;
 int ret;
'''+scan+r'''
unmap: (void)s2cr; return ret;
}
int main(void) {
 page[0x20/4]=BIT(27)|255;
 page[(0x800+4*14)/4]=BIT(31)|0x520;
 page[(0x800+4*170)/4]=BIT(31)|(1<<16)|0x520;
 page[(0x800+4*254)/4]=BIT(31)|0x522;
 assert(run()==0 && reads==2+255+4);
 assert(matches[0]==2 && matches[1]==1 && matches[2]==1);
 memset(page,0,sizeof(page));reads=0;
 page[0x20/4]=BIT(27)|BIT(8)|255;
 assert(run()==-EOPNOTSUPP && reads==2);
 page[0x20/4]=BIT(27)|255;page[0]=BIT(3);reads=0;
 assert(run()==-EOPNOTSUPP && reads==2);
 page[0]=0;page[0x20/4]=0;reads=0;
 assert(run()==-EINVAL && reads==2);
 puts("IPA_SMMU_PASS: bounded255 groups, exact/masked IPA range, EXID and invalid guards");
}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td);(p/'test.c').write_text(h)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-no-pie',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
