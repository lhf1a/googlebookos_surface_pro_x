#include <assert.h>
#include <string.h>
#include "../probe/test2/sq2-ipa-acpi.h"
#include "../probe/camera/spx-camera-acpi.h"
static void fix(unsigned char *p, unsigned int n) {
 unsigned int i; p[9] = 0;
 for (i = 0; i < n; i++) if (i != 9) p[9] -= p[i];
}
int main(void) {
 unsigned char b[148] = {0}; unsigned long long addr;
 memcpy(b, "FACP", 4); b[4] = sizeof(b);
 b[40] = 0x34; b[41] = 0x12; fix(b, sizeof(b));
 assert(!spx_camera_dsdt_address(b, sizeof(b), &addr) && addr == 0x1234);
 b[140] = 0x78; b[144] = 1; fix(b, sizeof(b));
 assert(!spx_camera_dsdt_address(b, sizeof(b), &addr) && addr == 0x100000078ULL);
 b[10] ^= 1; assert(spx_camera_dsdt_address(b, sizeof(b), &addr));
 b[10] ^= 1; b[4] = 44; fix(b, 44);
 assert(!spx_camera_dsdt_address(b, 44, &addr) && addr == 0x1234);
 assert(spx_camera_dsdt_address(b, 43, &addr));
 b[40] = b[41] = 0; fix(b, 44);
 assert(spx_camera_dsdt_address(b, 44, &addr));
 b[0] = 'X'; fix(b, 44); assert(spx_camera_dsdt_address(b, 44, &addr));
 return 0;
}
