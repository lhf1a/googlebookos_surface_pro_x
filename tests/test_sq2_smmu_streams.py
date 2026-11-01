"""Run the real read loop against a bounded fake GR0 page."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t u32;
#define __iomem
static u32 page[1024], seen[1024];
static unsigned reads;
static u32 readl(const void *address) {
 uintptr_t offset = (uintptr_t)address - (uintptr_t)page;
 assert((offset >= 0x800 && offset < 0xa00) ||
        (offset >= 0xc00 && offset < 0xe00));
 assert(!(offset & 3));
 assert(!seen[offset / 4]++);
 reads++;
 return page[offset / 4];
}
#include "sq2-smmu-streams.h"
int main(void) {
 struct { u32 first; struct sq2_smmu_streams data; u32 last; } result;
 u32 saved[1024];
 for (unsigned i = 0; i < 1024; i++) page[i] = 0x12340000 + i;
 memcpy(saved, page, sizeof(page));
 memset(&result, 0xa5, sizeof(result));
 sq2_smmu_streams_read(page, &result.data);
 assert(reads == 256);
 assert(result.first == 0xa5a5a5a5 && result.last == 0xa5a5a5a5);
 assert(!memcmp(page, saved, sizeof(page)));
 for (unsigned i = 0; i < 128; i++) {
  assert(result.data.smr[i] == 0x12340200 + i);
  assert(result.data.s2cr[i] == 0x12340300 + i);
 }
 return 0;
}
'''


class SmmuStreamsTest(unittest.TestCase):
    def test_read_set_bounds_and_no_writes(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'test.c'
            output = Path(directory) / 'test'
            source.write_text(HARNESS)
            subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-I', str(ROOT / 'probe/video'),
                            str(source), '-o', str(output)], check=True)
            subprocess.run([str(output)], check=True)


if __name__ == '__main__':
    unittest.main()
