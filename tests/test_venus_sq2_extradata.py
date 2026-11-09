'''Firmware extradata selection and DMA-plane safety; no hardware claimed.'''
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

spec = importlib.util.spec_from_file_location('patch_extra', ROOT/'scripts/patch-sq2-venus-extradata.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)

class Extradata(unittest.TestCase):
    def test_property_selection_and_plane_bounds(self):
        with tempfile.TemporaryDirectory() as td:
            exe = Path(td)/'test'
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                '-fsanitize=address,undefined','-fno-pie','-no-pie',
                '-I'+str(ROOT/'probe/video'),
                str(ROOT/'tests/fixtures/quest2_video/venus-extradata-test.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)


    def test_dpb_size_failure_has_single_cleanup_owner(self):
        fixture = (ROOT/'tests/fixtures/quest2_video/venus-dpb-allocation.c').read_text()
        harness = (ROOT/'tests/fixtures/quest2_video/venus-dpb-allocation-test.c').read_text()
        repaired = patch.fix_dpb_cleanup(fixture)
        self.assertEqual(patch.fix_dpb_cleanup(repaired), repaired)
        with tempfile.TemporaryDirectory() as td:
            for name, allocator in [('old', fixture), ('fixed', repaired)]:
                source = Path(td)/(name+'.c')
                exe = Path(td)/name
                source.write_text(harness.replace('/* ALLOCATOR */', allocator))
                subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-pie','-no-pie',
                    '-I'+str(ROOT/'probe/video'),str(source),'-o',str(exe)],check=True)
                result = subprocess.run([str(exe)],text=True,capture_output=True,timeout=10)
                if name == 'old':
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('attempting double-free', result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout+result.stderr)

    def test_dpb_cleanup_rejects_unknown_or_duplicate_source(self):
        with self.assertRaises(ValueError):
            patch.fix_dpb_cleanup('unknown helpers source')
        with self.assertRaises(ValueError):
            patch.fix_dpb_cleanup(patch.OLD_DPB_EXTRA_SIZE*2)
        with self.assertRaises(ValueError):
            patch.fix_dpb_cleanup(patch.OLD_DPB_EXTRA_SIZE+patch.DPB_EXTRA_SIZE)

    def test_firmware_requirement_count_checked_before_copy(self):
        preamble = """
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
#define HFI_BUFFER_TYPE_MAX 11
#define HFI_ERR_SESSION_INVALID_PARAMETER 1
#define HFI_ERR_NONE 0
struct hfi_buffer_requirements {u32 type,size,region,hold,min,actual,contiguous,alignment;};
struct hfi_msg_session_property_info_pkt {
 struct {struct {u32 size,type;} hdr;u32 session_id;} shdr;
 u32 count,property,data[1];
};
"""
        main = """
int main(void) {
 for(unsigned n=1;n<=12;n++) {
  struct hfi_msg_session_property_info_pkt *p=calloc(1,sizeof(*p)+n*sizeof(struct hfi_buffer_requirements));
  struct hfi_buffer_requirements *out=calloc(11,sizeof(*out));
  p->shdr.hdr.size=sizeof(*p)+n*sizeof(*out);p->data[0]=1;
  unsigned rc=session_get_prop_buf_req(p,out);
  assert(rc==(n<=11?HFI_ERR_NONE:HFI_ERR_SESSION_INVALID_PARAMETER));
  if(n>11)assert(out[0].type==0);
  free(out);free(p);
 }
 struct hfi_msg_session_property_info_pkt p={0};
 struct hfi_buffer_requirements out[11]={0};
 assert(session_get_prop_buf_req(&p,out)==HFI_ERR_SESSION_INVALID_PARAMETER);
 p.shdr.hdr.size=sizeof(p)+1;p.data[0]=1;
 assert(session_get_prop_buf_req(&p,out)==HFI_ERR_SESSION_INVALID_PARAMETER);
 return 0;
}
"""
        fixture=(ROOT/'tests/fixtures/quest2_video/venus-buffer-requirements.c').read_text()
        with tempfile.TemporaryDirectory() as td:
            source=Path(td)/'test.c';exe=Path(td)/'test'
            source.write_text(preamble+patch.patch_buffer_requirements(fixture)+main)
            subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                '-fno-pie','-no-pie',str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True,timeout=10)

if __name__=='__main__':
    unittest.main()
