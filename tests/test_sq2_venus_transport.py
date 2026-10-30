"""Exercise the actual pinned-source preparer; no device access."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
KERNEL = Path('/root/.cache/sq2-thermal/kernel')


@unittest.skipUnless(KERNEL.exists(), 'pinned WSL kernel tree required')
class VenusTransport(unittest.TestCase):
    def test_decoder_overlay_matches_pinned_driver(self):
        driver = (KERNEL / 'drivers/media/platform/qcom/venus/vdec.c').read_text()
        overlay = (ROOT / 'scripts/build-sq2-hfi-overlay.py').read_text()
        self.assertIn('.compatible = "venus-decoder"', driver)
        self.assertIn('video-decoder { compatible = "venus-decoder"; };', overlay)
        self.assertNotIn('compatible = "qcom,venus-decoder"', overlay)

    def test_lab_resources_and_buffer_checks(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'resources'
            cmd = [sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                   '--output', str(dest), '--sq2-resources']
            rejected = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(dest.exists())
            subprocess.run(cmd + ['--firmware-lease', '--platform-entry'], check=True)
            core = (dest / 'core.c').read_text()
            helpers = (dest / 'helpers.c').read_text()
            res = (dest / 'venus-sq2-resources.h').read_text()
            self.assertIn('"microsoft,sq2-venus-lab", .data = &sq2_lab_res', core)
            self.assertIn('.cp_size = 0x25800000', res)
            self.assertNotIn('0x70800000', res)
            self.assertEqual(helpers.count('!sq2_venus_buffer_valid('), 4)
            self.assertEqual(helpers.count('ret = sq2_venus_scope_bind('), 3)
            self.assertEqual(helpers.count('sq2_intbuf_alloc(inst, buf)'), 2)
            self.assertEqual(helpers.count('sq2_intbuf_dma_free(inst, buf)'), 6)
            self.assertNotIn('dma_alloc_attrs(', helpers)
            self.assertNotIn('dma_free_attrs(', helpers)
            self.assertIn('!sq2_venus_scope_ended(inst, &buf->sq2_retired)', helpers)
            self.assertIn('sq2_venus_scope_bind(inst, vb->planes[0].mem_priv)', helpers)
            session_end = (dest / 'venus-sq2-session-end.h').read_text()
            self.assertLess(session_end.index('inst->sq2_end_confirmed = true;'), session_end.index('sq2_venus_scope_end(inst);'))
            vdec = (dest / 'vdec.c').read_text()
            self.assertEqual(vdec.count('MODULE_IMPORT_NS(DMA_BUF);'), 1)
            self.assertIn('if (!bytesused && (!IS_SQ2_HFI(inst->core) ||', vdec)
            self.assertEqual(vdec.count('sq2_vdec_trace(inst, "'), 16)
            trace = vdec[vdec.index('static bool sq2_stage_trace;'):vdec.index('\n\n/*', vdec.index('static bool sq2_stage_trace;'))]
            harness = '''#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
typedef int atomic_t;
#define ATOMIC_INIT(x) (x)
#define module_param(a,b,c)
struct core { bool sq2; int dev; };
struct venus_inst { struct core *core; };
#define IS_SQ2_HFI(c) ((c)->sq2)
static unsigned logs;
static bool atomic_add_unless(int *v,int n,int limit) { if (*v==limit) return false; *v+=n; return true; }
#define dev_info(dev,fmt,stage,ret) do { (void)(dev); (void)(fmt); (void)(stage); (void)(ret); logs++; } while (0)
''' + trace + '''
int main(void) {
 struct core c={.sq2=true}; struct venus_inst i={.core=&c};
 sq2_vdec_trace(&i,"off",0); assert(logs==0 && sq2_stage_records==0);
 sq2_stage_trace=true; c.sq2=false;
 sq2_vdec_trace(&i,"legacy",0); assert(logs==0 && sq2_stage_records==0);
 c.sq2=true; for(int n=0;n<1000;n++) sq2_vdec_trace(&i,"test",-5);
 assert(logs==128 && sq2_stage_records==128);
 puts("STAGE_TRACE_PASS default-off legacy-zero saturated-128"); return 0;
}
'''
            (dest / 'trace-test.c').write_text(harness)
            subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                            '-fno-pie', '-no-pie', str(dest / 'trace-test.c'), '-o', str(dest / 'trace-test')], check=True)
            subprocess.run([str(dest / 'trace-test')], check=True)
            start_path = vdec[vdec.index('static int vdec_start_streaming('):vdec.index('static void vdec_cancel_dst_buffers(')]
            self.assertLess(start_path.index('inst->session_error ||'), start_path.index('vdec_start_capture(inst)'))
            self.assertEqual(vdec.count('if (IS_SQ2_HFI(core)) abort |='), 5)
            self.assertEqual(vdec.count('&sq2_vb2_memops : &vb2_dma_contig_memops;'), 2)
            close = vdec[vdec.index('static int vdec_close('):vdec.index('static const struct v4l2_file_operations')]
            self.assertLess(close.index('sq2_callback_close_begin(inst);'), close.index('v4l2_m2m_ctx_release('))
            msgs = (dest / 'hfi_msgs.c').read_text()
            self.assertIn('if (!sq2_callback_get(inst, packet)) break;', msgs)
            self.assertIn('handler->done(core, inst, hdr);\n\tsq2_callback_put(inst);', msgs)
            session = (dest / 'hfi.c').read_text()
            destroy = session[session.index('void hfi_session_destroy('):session.index('EXPORT_SYMBOL_GPL(hfi_session_destroy);')]
            self.assertLess(destroy.index('list_del_init('), destroy.index('sq2_callback_drain(inst);'))
            self.assertLess(destroy.index('mutex_unlock('), destroy.index('sq2_callback_drain(inst);'))
            self.assertLess(close.index('hfi_session_destroy(inst);'), close.index('venus_helper_sq2_retire_buffers(inst);'))
            self.assertLess(close.index('venus_helper_sq2_retire_buffers(inst);'), close.index('kfree(inst);'))
            dpb = helpers[helpers.index('static void free_dpb_buf('):helpers.index('int venus_helper_queue_dpb_bufs(')]
            self.assertLess(dpb.index('sq2_intbuf_retire(inst, buf);'), dpb.index('ida_free('))
            self.assertIn('return;', dpb[:dpb.index('ida_free(')])
            self.assertIn('sq2_venus_retire_stopped(core);', (dest / 'venus-sq2-core.h').read_text())
            self.assertIn('!sq2_venus_payload_valid(', helpers)
            section = helpers[helpers.index('int venus_helper_vb2_buf_init('):]
            self.assertLess(section.index('!sq2_venus_buffer_valid('), section.index('buf->dma_addr ='))

    def test_independent_core_dispatch(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'entry'
            cmd = [sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                   '--output', str(dest), '--platform-entry']
            rejected = subprocess.run(cmd, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertFalse(dest.exists())
            subprocess.run(cmd + ['--firmware-lease'], check=True)
            core = (dest / 'core.c').read_text()
            entry = (dest / 'venus-sq2-core.h').read_text()
            self.assertLess(core.index('return venus_sq2_probe(pdev, res);'),
                            core.index('core = devm_kzalloc'))
            self.assertIn('return venus_sq2_remove(pdev);', core)
            self.assertIn('venus_sq2_stop_core(core);', core)
            self.assertIn('if (!sq2_platform_allow) return -EOPNOTSUPP;', entry)
            self.assertNotIn('.sq2_windows_hfi =', core)
            self.assertEqual(entry.count('ret = venus_boot(core);'), 1)
            worker = entry[entry.index('static void venus_sq2_error'):entry.index('static int venus_sq2_remove')]
            self.assertNotIn('ret = venus_boot', worker)
            self.assertNotIn('schedule_delayed_work(', worker)
            self.assertLess(entry.index('ret = hfi_core_init(core);'),
                            entry.index('ret = of_platform_populate'))
            self.assertIn('if (stop || core->sq2_public)', entry)
            self.assertIn('false : venus_fw_low_power_mode', (dest / 'hfi_venus.c').read_text())

    def test_pas_dispatch_is_opt_in_and_probe_is_blocked(self):
        with tempfile.TemporaryDirectory() as tmp:
            for lease in (False, True):
                dest = Path(tmp) / str(lease)
                cmd = [sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                       '--output', str(dest)]
                if lease:
                    cmd.append('--firmware-lease')
                subprocess.run(cmd, check=True)
                fw = (dest / 'firmware.c').read_text()
                core = (dest / 'core.c').read_text()
                self.assertEqual('return venus_sq2_boot(core);' in fw, lease)
                self.assertEqual('return venus_sq2_shutdown(core);' in fw, lease)
                self.assertIn('if (IS_SQ2_HFI(core))\n\t\treturn -EOPNOTSUPP;', core)
                self.assertNotIn('.sq2_windows_hfi =', core)
                if lease:
                    self.assertLess(fw.index('return venus_sq2_boot(core);'),
                                    fw.index('ret = venus_load_fw(core,'))
                    self.assertIn('core->sq2_boot_attempted = true;',
                                  (dest / 'venus-sq2-firmware.h').read_text())

    def test_generated_transport_and_legacy_isolation(self):
        with tempfile.TemporaryDirectory() as tmp:
            dest = Path(tmp) / 'venus'
            subprocess.run([sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                            '--kernel', str(KERNEL), '--output', str(dest)], check=True)
            hfi = (dest / 'hfi_venus.c').read_text()
            self.assertIn('venus_sq2_cpu_map(hdev->core,', hfi)
            self.assertIn('desc.da += ALIGNED_QUEUE_SIZE;', hfi)
            self.assertIn('desc.kva += ALIGNED_QUEUE_SIZE;', hfi)
            self.assertIn('if (!IS_SQ2_HFI(hdev->core))\n\t\tvenus_free(hdev, &hdev->sfr);', hfi)
            self.assertIn('if (IS_IRIS1(hdev->core) && !IS_SQ2_HFI(hdev->core))', hfi)
            self.assertIn('if (!IS_SQ2_HFI(hdev->core))\n\t\twritel(1, cpu_cs_base + CPU_CS_SCIACMDARG3);', hfi)
            self.assertIn('sq2_venus_dma_check_uc(hdev->core->dev, desc.da, desc.size)', hfi)
            self.assertIn('sq2_venus_dma_begin(core->dev, &hdev->sq2_dma)', hfi)
            self.assertEqual(hfi.count('sq2_venus_dma_restore(core->dev, &hdev->sq2_dma);'), 2)
            self.assertLess(hfi.index('venus_interface_queues_release(hdev);'),
                            hfi.index('sq2_venus_dma_restore(core->dev, &hdev->sq2_dma);'))
            self.assertNotIn('.sq2_windows_hfi =', (dest / 'core.c').read_text())
            manifest = json.loads((dest / 'candidate.json').read_text())
            self.assertIn('no-platform-match', manifest['status'])
            # Refuse reuse, rather than overwrite a prior candidate.
            again = subprocess.run([sys.executable, str(ROOT / 'scripts/prepare-sq2-venus-iris1.py'),
                                    '--output', str(dest)], capture_output=True, text=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertIn('output exists', again.stderr)


if __name__ == '__main__':
    unittest.main()
