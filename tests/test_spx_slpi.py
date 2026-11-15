import importlib.util
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec=importlib.util.spec_from_file_location(name,ROOT/"scripts"/(name+".py"))
    m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
    return m


dt=load("prepare-spx-slpi-dtb")
driver=load("build-spx-slpi")
efi=load("prepare-spx-slpi-efi")


def firmware(flags=0x8000005):
    b=bytearray(88)
    b[:7]=b"\x7fELF\x01\x01\x01"
    struct.pack_into("<H",b,18,164)
    struct.pack_into("<I",b,28,52)
    struct.pack_into("<HH",b,42,32,1)
    struct.pack_into("<8I",b,52,1,84,0xb0100000,0x97300000,4,0x1400000,flags,0x100000)
    return bytes(b)


def tree():
    source="""/dts-v1/;
    / {
     compatible="microsoft,surface-pro-x","qcom,sc8180x";
     #address-cells=<2>; #size-cells=<2>;
     reserved-memory {
      #address-cells=<2>; #size-cells=<2>; ranges;
      scss@99000000 { reg=<0 0x99000000 0 0x1400000>; no-map; };
      adsp@96e00000 { reg=<0 0x96e00000 0 0x1a00000>; no-map; };
     };
     gic { compatible="arm,gic-v3"; };
     rpmhcc { compatible="qcom,sc8180x-rpmh-clk"; phandle=<973>; };
     rpmhpd { compatible="qcom,sc8180x-rpmhpd"; };
     qmp { compatible="qcom,sc8180x-aoss-qmp"; };
     mailbox { compatible="qcom,sc8180x-apss-shared"; };
     smmu@15000000 { compatible="qcom,sc8180x-smmu-500"; reg=<0 0x15000000 0 0x100000>; };
     smmu@2ca0000 { compatible="qcom,sc8180x-smmu-500"; reg=<0 0x2ca0000 0 0x10000>; };
     smp2p-slpi { master-kernel {}; slave-kernel {}; };
     soc { remoteproc@17300000 { compatible="qcom,sc8180x-adsp-pas"; reg=<0 0x17300000 0 0x1000>; }; };
    };"""
    return subprocess.check_output(["dtc","-q","-I","dts","-O","dtb"],input=source.encode())


class SlpiTests(unittest.TestCase):
    def test_relocation_and_existing_resources_preserved(self):
        before=tree()
        after=dt.prepare(before,firmware(),"qcom/test/slpi.mbn")
        a=dt.dt.properties(libfdt.Fdt(before)); b=dt.dt.properties(libfdt.Fdt(after))
        self.assertTrue(all(b[k]==v for k,v in a.items()))
        self.assertEqual(b["/soc/remoteproc@2400000","microsoft,el2-preboot-complete"],dt.cells(0))
        self.assertEqual(b["/soc/remoteproc@2400000","firmware-name"],b"qcom/test/slpi.mbn\0")
        memory=int.from_bytes(b["/soc/remoteproc@2400000","memory-region"],"big")
        self.assertGreater(memory,973)
        self.assertEqual(b["/reserved-memory/scss@99000000","phandle"],dt.cells(memory))
        with self.assertRaises(ValueError):
            dt.prepare(after,firmware(),"qcom/test/slpi.mbn")

    def test_firmware_and_memory_rejection(self):
        for image in (b"short",firmware(5),firmware()[:-1]):
            with self.assertRaises(ValueError): dt.firmware_span(image)
        f=libfdt.Fdt(tree()); f.resize(len(f.as_bytearray())+1024)
        f.setprop(f.path_offset("/reserved-memory/adsp@96e00000"),"reg",dt.cells(0,0x98000000,0,0x2000000))
        with self.assertRaisesRegex(ValueError,"overlapping"):
            dt.prepare(bytes(f.as_bytearray()),firmware(),"qcom/test.mbn")
        with self.assertRaises(ValueError): dt.prepare(tree(),firmware(),"../other")

    def test_pas_driver_does_not_claim_audio_or_modem(self):
        text='''/* SQ2_EL2_PREBOOT_HANDOFF */
if (!(of_device_is_compatible(np, "qcom,sc8180x-adsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-cdsp-pas") ||
	      of_device_is_compatible(np, "qcom,sc8180x-mpss-pas"))) return -EINVAL;
static const struct of_device_id qcom_pas_of_match[] = {
 { .compatible = "qcom,sc8180x-adsp-pas" },
};
.name = "qcom_q6v5_pas"
'''
        result=driver.source_for_slpi(text)
        self.assertNotIn('"qcom,sc8180x-adsp-pas"',result)
        self.assertIn('"qcom,sc8180x-slpi-pas"',result)
        self.assertIn("return -EINVAL",result)
        with self.assertRaises(ValueError): driver.source_for_slpi("unknown")

    def test_efi_handoff_copies_only_completed_result(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); src=root/"source"; (src/"src").mkdir(parents=True)
            (src/"src/pil-types.c").write_text("static const struct pil_type pil_types[] = {\n};")
            (src/"src/sq2-preboot-rmtfs.c").write_text(
                "EFI_STATUS sq2_preboot_publish_results(void) {\n"
                "\tWriteBackInvalidateDataCacheRange(dest, fdt_totalsize(dest));\n}")
            efi.prepare(src,root/"output")
            s=(root/"output/src/sq2-preboot-rmtfs.c").read_text()
            helper=s[s.index("static EFI_STATUS spx_slpi_publish"):s.index("EFI_STATUS sq2_preboot_publish_results")]
            harness=r"""
#include <assert.h>
#include <stdint.h>
#include <libfdt.h>
typedef int EFI_STATUS;
#define EFI_SUCCESS 0
#define EFI_INVALID_PARAMETER 1
#define EFI_NOT_READY 2
static void *result_dtb;
""" + helper + r"""
static void init(void *b) {
 assert(!fdt_create_empty_tree(b,4096));
 int soc=fdt_add_subnode(b,0,"soc@0");
 int n=fdt_add_subnode(b,soc,"remoteproc@2400000");
 assert(n>=0);
 assert(!fdt_setprop_string(b,n,"compatible","qcom,sc8180x-slpi-pas"));
 assert(!fdt_setprop(b,n,"microsoft,el2-preboot",NULL,0));
 assert(!fdt_setprop_u32(b,n,"microsoft,el2-preboot-complete",0));
 uint32_t z[6]={0};
 assert(!fdt_setprop(b,n,"microsoft,el2-preboot-status",z,sizeof(z)));
}
int main(void) {
 uint64_t a[512],b[512]; result_dtb=a;
 assert(!fdt_create_empty_tree(a,sizeof(a))); assert(!fdt_create_empty_tree(b,sizeof(b)));
 assert(spx_slpi_publish(b)==EFI_SUCCESS);
 init(a); assert(spx_slpi_publish(b)==EFI_INVALID_PARAMETER);
 init(b); assert(spx_slpi_publish(b)==EFI_NOT_READY);
 int n=fdt_path_offset(a,"/soc@0/remoteproc@2400000");
 fdt_setprop_u32(a,n,"microsoft,el2-preboot-complete",1);
 uint32_t trace[6]={1,2,3,4,5,6};
 fdt_setprop(a,n,"microsoft,el2-preboot-status",trace,sizeof(trace));
 assert(spx_slpi_publish(b)==EFI_SUCCESS);
 n=fdt_path_offset(b,"/soc@0/remoteproc@2400000");
 int len; const void *p=fdt_getprop(b,n,"microsoft,el2-preboot-status",&len);
 assert(len==24 && !memcmp(p,trace,24));
 fdt_setprop_string(b,n,"compatible","other,dsp");
 assert(spx_slpi_publish(b)==EFI_INVALID_PARAMETER);
}
"""
            (root/"test.c").write_text(harness)
            subprocess.run(["cc","-Wall","-Wextra","-Werror","-include","string.h","-fsanitize=address,undefined",
                            "-fno-pie","-no-pie",str(root/"test.c"),"-lfdt","-o",str(root/"test")],check=True)
            subprocess.run([str(root/"test")],check=True)


if __name__=="__main__": unittest.main()
