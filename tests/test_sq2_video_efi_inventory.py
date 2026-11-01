"""Check the actual read-only lookup core without EFI or device operations."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'probe/virtualization/sq2-video-efi-inventory.c').read_text()
source = source[:source.index('static EFI_STATUS inventory(')]
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef uint64_t EFI_STATUS;
typedef char CHAR8;
typedef int BOOLEAN;
typedef void VOID;
#define EFIAPI
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_UNSUPPORTED 3
#define EFI_BUFFER_TOO_SMALL 5
#define CopyMem memcpy
'''
test = r'''
static int clock_calls, domain_calls;
static EFI_STATUS get_clock(struct sq2_clock *c, const CHAR8 *name, UINTN *id) {
 assert(c); assert(*id == ~(UINTN)0);
 assert(!strcmp(name, clocks[clock_calls])); clock_calls++;
 if (strstr(name, "negative_control")) return 0x8000000000000018ULL;
 *id = clock_calls; return 0;
}
static EFI_STATUS get_domain(struct sq2_clock *c, const CHAR8 *name, UINTN *id) {
 assert(c); assert(*id == ~(UINTN)0);
 assert(!strcmp(name, domains[domain_calls])); domain_calls++;
 if (strstr(name, "negative_control")) return 0x8000000000000018ULL;
 *id = domain_calls + 100; return 0;
}
int main(void) {
 _Static_assert(offsetof(struct sq2_clock, clock_id) == 8, "clock ABI");
 _Static_assert(offsetof(struct sq2_clock, power_id) == 80, "power ABI");
 struct sq2_clock c = {.version=0x10006, .clock_id=get_clock, .power_id=get_domain};
 assert(collect(&c) == EFI_SUCCESS);
 assert(clock_calls == 8 && domain_calls == 4);
 assert(used == strlen(report) && used < sizeof(report));
 assert(strstr(report, "version=0000000000010006"));
 assert(strstr(report, "mvs1_gdsc status=0000000000000000 id=0000000000000067"));
 assert(strstr(report, "negative_control status=8000000000000018 id=ffffffffffffffff"));
 clock_calls=domain_calls=0;
 c.version=0x10009;
 assert(collect(&c) == EFI_UNSUPPORTED && !clock_calls && !domain_calls);
 assert(strstr(report, "no lookup performed"));
 c.version=0x10006; c.clock_id=NULL;
 assert(collect(&c) == EFI_UNSUPPORTED && !clock_calls && !domain_calls);
 c.clock_id=get_clock; c.power_id=NULL;
 assert(collect(&c) == EFI_UNSUPPORTED && !clock_calls && !domain_calls);
 used=sizeof(report)-2; report[used]=0;
 assert(!append("too long") && used==sizeof(report)-2);
 assert(append("x") && used==sizeof(report)-1 && report[used]==0);
 assert(!append("x"));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = Path(tmp) / 'inventory.c'
    exe = Path(tmp) / 'inventory'
    c.write_text(prefix + source + test)
    subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-pie', '-no-pie', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: ABI offsets, exact lookup list, negative controls, version/null rejection and report bounds')
