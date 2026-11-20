"""Verify SPX front/rear/IR multi-IFE CAMSS DT generation and camera kernel patches."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import libfdt

ROOT = Path(__file__).resolve().parents[1]
_DEFAULT_SOURCES = Path.home() / "spx-full-image/kernel-20261110-v6-sources"
KSRC = Path(os.environ.get("SPX_KERNEL_SRC", str(_DEFAULT_SOURCES / "kernel")))
sys.path.insert(0, str(ROOT / "scripts"))
import full_image_camera as camera


def build_base_spx_dtb():
    dts = """/dts-v1/;
/memreserve/ 0x81234000 0x2000;
/ {
    compatible = "microsoft,surface-pro-x", "qcom,sc8180x";
    #address-cells = <2>;
    #size-cells = <2>;
    chosen { marker = "preserve"; };
    interconnect-mc-virt { compatible = "qcom,sc8180x-mc-virt"; phandle = <18>; };
    soc@0 {
        compatible = "simple-bus";
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;
        clock-controller@100000 { compatible = "qcom,gcc-sc8180x"; phandle = <11>; };
        interconnect@1500000 { compatible = "qcom,sc8180x-config-noc"; phandle = <16>; };
        interconnect@1740000 { compatible = "qcom,sc8180x-mmss-noc"; phandle = <17>; };
        pinctrl@3100000 { compatible = "qcom,sc8180x-tlmm"; phandle = <13>; };
        interconnect@9680000 { compatible = "qcom,sc8180x-gem-noc"; phandle = <15>; };
        clock-controller@ad00000 { compatible = "qcom,sc8180x-camcc"; phandle = <42>; };
        spmi@c440000 { pmic@1 { }; };
        iommu@15000000 { compatible = "qcom,sc8180x-smmu-500"; phandle = <14>; };
        interrupt-controller@17a00000 { compatible = "arm,gic-v3"; phandle = <12>; };
        regulators-a {
            compatible = "qcom,pmc8180-rpmh-regulators";
            qcom,pmic-id = "a";
            vdd-l1-l8-l11-supply = <12>;
            vdd-l7-l12-l14-l15-supply = <12>;
            vdd-l13-l16-l17-supply = <12>;
            ldo7 { regulator-min-microvolt = <1800000>; };
        };
    };
};
"""
    return subprocess.check_output(["dtc", "-q", "-I", "dts", "-O", "dtb"], input=dts.encode())


def unpack_u32(prop_bytes):
    assert len(prop_bytes) % 4 == 0
    return struct.unpack(">" + "I" * (len(prop_bytes) // 4), prop_bytes)


def unpack_strings(prop_bytes):
    return [s.decode() for s in prop_bytes.rstrip(b"\0").split(b"\0") if s]


class CameraIRMultiIFEDTBTest(unittest.TestCase):
    def test_full_pipeline_generates_three_sensors_multi_ife_and_privacy_leds(self):
        base = build_base_spx_dtb()
        prepared = camera.prepare_dtb(base)
        # Idempotent when run on an already-prepared DTB.
        self.assertEqual(camera.prepare_dtb(prepared), prepared)

        f = libfdt.Fdt(prepared)
        self.assertEqual(f.get_mem_rsv(0)[1:], [0x81234000, 0x2000])
        self.assertEqual(bytes(f.getprop(f.path_offset("/chosen"), "marker")), b"preserve\0")

        # Verify on-demand qcom_spmi-regulator model (no regulator-always-on, no dummy regulator).
        l14_off = f.path_offset("/soc@0/spmi@c440000/pmic@1/camera-regulators/l14")
        l14_ph = f.get_phandle(l14_off)
        self.assertGreater(l14_ph, 0)
        self.assertEqual(f.getprop(l14_off, "regulator-always-on", quiet=(libfdt.NOTFOUND,)), -libfdt.NOTFOUND)

        # Verify OV7251 Front IR sensor node on CCI0 bus 0 @ 0x60.
        ir_path = "/soc@0/cci@ac4a000/i2c-bus@0/camera@60"
        ir_off = f.path_offset(ir_path)
        self.assertEqual(unpack_strings(bytes(f.getprop(ir_off, "compatible"))), ["ovti,ov7251"])
        self.assertEqual(unpack_u32(bytes(f.getprop(ir_off, "reg"))), (0x60,))
        self.assertEqual(unpack_u32(bytes(f.getprop(ir_off, "clocks"))), (42, 105))
        self.assertEqual(unpack_strings(bytes(f.getprop(ir_off, "clock-names"))), ["xclk"])
        self.assertEqual(unpack_u32(bytes(f.getprop(ir_off, "assigned-clock-rates"))), (19200000,))
        self.assertEqual(unpack_u32(bytes(f.getprop(ir_off, "clock-frequency"))), (19200000,))
        # GPIO 23 active-high (flag=0) via TLMM phandle 13, and no reset-gpios.
        self.assertEqual(unpack_u32(bytes(f.getprop(ir_off, "enable-gpios"))), (13, 23, 0))
        self.assertEqual(f.getprop(ir_off, "reset-gpios", quiet=(libfdt.NOTFOUND,)), -libfdt.NOTFOUND)
        self.assertEqual(unpack_u32(bytes(f.getprop(ir_off, "orientation"))), (0,))
        for supply in ("vdddo-supply", "vdda-supply", "vddd-supply"):
            self.assertEqual(unpack_u32(bytes(f.getprop(ir_off, supply))), (l14_ph,))

        ir_ep = f.path_offset(ir_path + "/port/endpoint")
        self.assertEqual(unpack_u32(bytes(f.getprop(ir_ep, "data-lanes"))), (1,))
        self.assertEqual(int.from_bytes(bytes(f.getprop(ir_ep, "link-frequencies")), "big"), 240000000)

        # Verify front OV5693 and rear OV13858 are still intact.
        self.assertEqual(
            unpack_strings(bytes(f.getprop(f.path_offset("/soc@0/cci@ac4a000/i2c-bus@1/camera@36"), "compatible"))),
            ["ovti,ov5693"],
        )
        self.assertEqual(
            unpack_strings(bytes(f.getprop(f.path_offset("/soc@0/cci@ac4a000/i2c-bus@0/camera@10"), "compatible"))),
            ["ovti,ov13858"],
        )

        # Verify SC8180X multi-IFE CAMSS node.
        isp_off = f.path_offset("/soc@0/isp@acb3000")
        self.assertEqual(unpack_strings(bytes(f.getprop(isp_off, "compatible"))), ["qcom,sc8180x-camss"])
        reg_names = unpack_strings(bytes(f.getprop(isp_off, "reg-names")))
        self.assertEqual(
            reg_names,
            [
                "csid0", "csid1", "csid_lite0", "csid_lite1",
                "csiphy0", "csiphy1", "csiphy2", "csiphy3",
                "vfe0", "vfe1", "vfe_lite0", "vfe_lite1",
            ],
        )
        self.assertEqual(unpack_strings(bytes(f.getprop(isp_off, "interrupt-names"))), reg_names)
        self.assertEqual(len(unpack_u32(bytes(f.getprop(isp_off, "reg")))), 12 * 4)
        self.assertEqual(len(unpack_u32(bytes(f.getprop(isp_off, "interrupts")))), 12 * 3)
        self.assertEqual(
            unpack_strings(bytes(f.getprop(isp_off, "power-domain-names"))),
            ["ife0", "ife1", "top"],
        )
        self.assertEqual(
            unpack_u32(bytes(f.getprop(isp_off, "power-domains"))),
            (42, 1, 42, 2, 42, 7),
        )
        self.assertEqual(
            unpack_strings(bytes(f.getprop(isp_off, "interconnect-names"))),
            ["ahb", "hf_0", "sf_0", "hf_1"],
        )
        self.assertEqual(len(unpack_u32(bytes(f.getprop(isp_off, "iommus")))), 16 * 3)

        # Verify refcounted privacy LED GPIOs: GPIO 25 front, GPIO 11 rear, active-high (0).
        self.assertEqual(unpack_u32(bytes(f.getprop(isp_off, "privacy-led-front-gpios"))), (13, 25, 0))
        self.assertEqual(unpack_u32(bytes(f.getprop(isp_off, "privacy-led-rear-gpios"))), (13, 11, 0))

        # Verify bidirectional endpoints for all 3 sensors (front->0, rear->2, ir->3).
        for sensor_path, port_num in (
            ("/soc@0/cci@ac4a000/i2c-bus@1/camera@36", 0),
            ("/soc@0/cci@ac4a000/i2c-bus@0/camera@10", 2),
            ("/soc@0/cci@ac4a000/i2c-bus@0/camera@60", 3),
        ):
            src_ep = f.path_offset(sensor_path + "/port/endpoint")
            dst_ep = f.path_offset(f"/soc@0/isp@acb3000/ports/port@{port_num}/endpoint")
            self.assertEqual(f.get_phandle(src_ep), unpack_u32(bytes(f.getprop(dst_ep, "remote-endpoint")))[0])
            self.assertEqual(f.get_phandle(dst_ep), unpack_u32(bytes(f.getprop(src_ep, "remote-endpoint")))[0])

        # Verify dtc can decompile the generated DTB without errors.
        dts_out = subprocess.check_output(["dtc", "-q", "-I", "dtb", "-O", "dts"], input=prepared)
        self.assertIn(b'compatible = "ovti,ov7251"', dts_out)
        self.assertIn(b"privacy-led-front-gpios", dts_out)
        self.assertIn(b"privacy-led-rear-gpios", dts_out)


class CameraPatchSeriesTest(unittest.TestCase):
    def test_patch_syntax_and_power_constraints(self):
        patch_dir = ROOT / "patches/camera"
        expected = [
            "0001-ov13858-of-power.patch",
            "0002-ov5693-power-sequence.patch",
            "0003-camss-sc8180x-initial.patch",
            "0007-camss-balanced-s-stream-unwind.patch",
            "0008-ov7251-continuous-clk-and-ir-strobe.patch",
            "0009-camss-privacy-leds-vfe-lite-fifo.patch",
        ]
        for name in expected:
            text = (patch_dir / name).read_text()
            self.assertIn("--- a/", text, name)
            self.assertIn("+++ b/", text, name)
            self.assertIn("@@ ", text, name)

        ov13858 = (patch_dir / "0001-ov13858-of-power.patch").read_text()
        self.assertIn("OV13858_ANA_GAIN_MIN\t\t0x80", ov13858)
        self.assertIn("OV13858_DGTL_GAIN_MIN\t\t0x400", ov13858)
        self.assertIn("OV13858_REG_FORMAT1\t\t0x3820", ov13858)
        self.assertIn("ov13858_power_on", ov13858)
        self.assertIn("devm_regulator_bulk_get", ov13858)

        ov7251 = (patch_dir / "0008-ov7251-continuous-clk-and-ir-strobe.patch").read_text()
        self.assertIn("{ 0x4800, 0x24 }", ov7251)
        self.assertIn("0x3b81", ov7251)
        self.assertIn("0x3b8e", ov7251)
        self.assertIn("0x3005, 0x08", ov7251)
        self.assertIn("pm_runtime_put_autosuspend", ov7251)

        camss_init = (patch_dir / "0003-camss-sc8180x-initial.patch").read_text()
        for token in ("csid_lite0", "csid_lite1", "vfe_lite0", "vfe_lite1", "ife1"):
            self.assertIn(token, camss_init)

        camss_fix = (patch_dir / "0009-camss-privacy-leds-vfe-lite-fifo.patch").read_text()
        for token in (
            "privacy-led-front",
            "privacy-led-rear",
            "camss_set_privacy_led",
            "VFE_DUMMY_BUF",
            "WM_ADDR_NO_SYNC_LITE_VAL",
            "!vfe_is_lite(vfe)",
            "STATUS1_WM_CLIENT_BUF_DONE",
            "VFE_CORE_CFG",
            "VFE_BUS_IRQ_CLEAR_GLOBAL",
            "MEDIA_BUS_FMT_Y10_2X8_PADHI_LE",
            "video_get_subdev_format",
        ):
            self.assertIn(token, camss_fix)
        # Ensure we do NOT force CAMSS clocks permanently on across idle/suspend.
        self.assertNotIn("keeping bus clocks active", camss_fix)
        self.assertNotIn("camss_runtime_suspend", camss_fix)
        self.assertNotIn("powered_on", camss_fix)

        # Verify build scripts reference the new patches and ov7251 module.
        self.assertIn("0008-ov7251-continuous-clk-and-ir-strobe.patch",
                      (ROOT / "scripts/build-spx-camera-probe.sh").read_text())
        self.assertIn("0009-camss-privacy-leds-vfe-lite-fifo.patch",
                      (ROOT / "scripts/build-spx-camss.sh").read_text())
        self.assertIn("ov7251", (ROOT / "scripts/pack-spx-camera.py").read_text())
        self.assertIn("ov7251", (ROOT / "probe/camera/spx-camera-boot").read_text())

    def test_patches_apply_cleanly_to_linux_6_18_60_when_source_present(self):
        tarball_env = os.environ.get("SPX_LINUX_TARBALL")
        tarball = Path(tarball_env) if tarball_env else None
        ksrc_present = (KSRC / "drivers/media/i2c/ov7251.c").is_file() and (
            KSRC / "drivers/media/platform/qcom/camss/camss.c"
        ).is_file()
        tarball_present = tarball is not None and tarball.is_file()
        if not ksrc_present and not tarball_present:
            self.skipTest("local Linux 6.18.60 kernel source not present (set SPX_KERNEL_SRC or SPX_LINUX_TARBALL)")

        with tempfile.TemporaryDirectory(prefix="cam-patch-check-") as td:
            root = Path(td)
            sensors_dir = root / "sensors"
            camss_dir = root / "camss"
            sensors_dir.mkdir()
            camss_dir.mkdir()

            if ksrc_present:
                for name in ("ov13858.c", "ov5693.c", "ov7251.c"):
                    shutil.copyfile(KSRC / "drivers/media/i2c" / name, sensors_dir / name)
                for src in (KSRC / "drivers/media/platform/qcom/camss").iterdir():
                    if src.is_file() and src.suffix in (".c", ".h"):
                        shutil.copyfile(src, camss_dir / src.name)
            else:
                subprocess.run(
                    [
                        "tar", "-xf", str(tarball), "-C", str(root),
                        "linux-6.18.60/drivers/media/i2c/ov13858.c",
                        "linux-6.18.60/drivers/media/i2c/ov5693.c",
                        "linux-6.18.60/drivers/media/i2c/ov7251.c",
                        "linux-6.18.60/drivers/media/platform/qcom/camss",
                    ],
                    check=True,
                )
                for name in ("ov13858.c", "ov5693.c", "ov7251.c"):
                    shutil.copyfile(root / "linux-6.18.60/drivers/media/i2c" / name, sensors_dir / name)
                for src in (root / "linux-6.18.60/drivers/media/platform/qcom/camss").iterdir():
                    if src.is_file() and src.suffix in (".c", ".h"):
                        shutil.copyfile(src, camss_dir / src.name)

            for name in (
                "0001-ov13858-of-power.patch",
                "0002-ov5693-power-sequence.patch",
                "0008-ov7251-continuous-clk-and-ir-strobe.patch",
            ):
                subprocess.run(
                    ["patch", "--batch", "--forward", "--fuzz=0", "-d", str(sensors_dir), "-p1"],
                    input=(ROOT / "patches/camera" / name).read_bytes(),
                    check=True,
                    capture_output=True,
                )
            for name in (
                "0003-camss-sc8180x-initial.patch",
                "0007-camss-balanced-s-stream-unwind.patch",
                "0009-camss-privacy-leds-vfe-lite-fifo.patch",
            ):
                subprocess.run(
                    ["patch", "--batch", "--forward", "--fuzz=0", "-d", str(camss_dir), "-p1"],
                    input=(ROOT / "patches/camera" / name).read_bytes(),
                    check=True,
                    capture_output=True,
                )


if __name__ == "__main__":
    unittest.main()
