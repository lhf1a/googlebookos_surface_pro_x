"""Execute the patched dispatcher: late session events must never reach NULL handlers."""
from pathlib import Path
import importlib.util
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("venus_omx_patch", ROOT/"scripts/patch-sq2-venus-omx.py")
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
PREAMBLE = r"""
#include <assert.h>
#include <stddef.h>
struct venus_core {void *dev;};
struct venus_inst {int unused;};
struct hfi_msg_event_notify_pkt {
 struct {struct {unsigned size,pkt_type;} hdr;unsigned session_id;} shdr;
 unsigned event_id,event_data1,event_data2,ext_event_data[1];
};
static unsigned late_logs;
static void record_late(void *dev,const char *fmt,unsigned session,unsigned event,unsigned data1,unsigned data2) {
 assert(dev && fmt && session==0x1234 && event>1 && data1==0x5678 && data2==0x9abc);
 late_logs++;
}
#define dev_warn_ratelimited record_late
enum {HFI_EVENT_SYS_ERROR=1,HFI_EVENT_SESSION_ERROR,HFI_EVENT_SESSION_SEQUENCE_CHANGED,
      HFI_EVENT_RELEASE_BUFFER_REFERENCE,HFI_EVENT_SESSION_PROPERTY_CHANGED,EVT_SYS_ERROR};
static unsigned system_errors,session_events;
static void event_sys_error(struct venus_core *c,unsigned event,void *p) {
 assert(c && p && event==EVT_SYS_ERROR);system_errors++;
}
static void event_session_error(struct venus_core *c,struct venus_inst *i,void *p) {
 assert(c && i && p);session_events++;
}
#define event_seq_changed event_session_error
#define event_release_buffer_ref event_session_error
"""
MAIN = r"""
int main(void) {
 struct venus_core core={.dev=&core};struct venus_inst inst={0};
 struct hfi_msg_event_notify_pkt p={0};
 hfi_event_notify(&core,NULL,NULL);
 for(p.event_id=HFI_EVENT_SESSION_ERROR;p.event_id<=HFI_EVENT_SESSION_PROPERTY_CHANGED;p.event_id++)
  hfi_event_notify(&core,NULL,&p);
 assert(!system_errors && !session_events && !late_logs);
 p.shdr.hdr.size=offsetof(struct hfi_msg_event_notify_pkt,ext_event_data);
 p.shdr.session_id=0x1234;p.event_data1=0x5678;p.event_data2=0x9abc;
 p.event_id=HFI_EVENT_SESSION_ERROR;hfi_event_notify(&core,NULL,&p);assert(late_logs==1);
 p.shdr.hdr.size--;hfi_event_notify(&core,NULL,&p);assert(late_logs==1);

 p.event_id=HFI_EVENT_SYS_ERROR;hfi_event_notify(&core,NULL,&p);assert(system_errors==1);
 for(p.event_id=HFI_EVENT_SESSION_ERROR;p.event_id<HFI_EVENT_SESSION_PROPERTY_CHANGED;p.event_id++)
  hfi_event_notify(&core,&inst,&p);
 assert(session_events==3);
 return 0;
}
"""
class LateEvents(unittest.TestCase):
    def test_patched_dispatch_keeps_system_errors_and_drops_absent_sessions(self):
        with tempfile.TemporaryDirectory() as td:
            directory=Path(td)
            source=directory/"hfi_msgs.c"
            source.write_text((ROOT/"tests/fixtures/quest2_video/venus-hfi-event-notify.c").read_text())
            patch.apply_late_events(directory)
            text=source.read_text()
            self.assertIn("dev_warn_ratelimited",text)
            self.assertIn("dev_err_ratelimited",text)
            source.write_text(PREAMBLE+text+MAIN)
            subprocess.run(["cc","-Wall","-Wextra","-Werror","-fsanitize=address,undefined",
                            "-fno-pie","-no-pie",str(source),"-o",str(directory/"check")],check=True)
            subprocess.run([str(directory/"check")],check=True,timeout=10)

    def test_incremental_guard_upgrade_preserves_ratelimits(self):
        with tempfile.TemporaryDirectory() as td:
            directory = Path(td)
            source = directory / "hfi_msgs.c"
            original = (ROOT / "tests/fixtures/quest2_video/venus-hfi-event-notify.c").read_text()
            old_guard = original.replace(
                "\tif (!packet)\n\t\treturn;",
                "\t/* Closing sessions reject callbacks; only SYS_ERROR is sessionless. */\n"
                "\tif (!packet || (!inst && pkt->event_id != HFI_EVENT_SYS_ERROR))\n\t\treturn;")
            old_guard = old_guard.replace("dev_warn(dev,", "dev_warn_ratelimited(dev,")
            old_guard = old_guard.replace("dev_err(dev,", "dev_err_ratelimited(dev,")
            self.assertNotEqual(old_guard, original)
            self.assertIn("if (!packet || (!inst", old_guard)
            source.write_text(old_guard)
            patch.apply_late_events(directory)
            self.assertIn("late HFI event session:", source.read_text())
            self.assertEqual(source.read_text().count("dev_err_ratelimited"), 1)

    def test_unknown_anchor_does_not_partially_write(self):
        with tempfile.TemporaryDirectory() as td:
            directory=Path(td);source=directory/"hfi_msgs.c"
            original=(ROOT/"tests/fixtures/quest2_video/venus-hfi-event-notify.c").read_text().replace("dev_err(dev,", "unknown(dev,")
            source.write_text(original)
            with self.assertRaises(ValueError):
                patch.apply_late_events(directory)
            self.assertEqual(source.read_text(),original)

if __name__=="__main__":
    unittest.main()
