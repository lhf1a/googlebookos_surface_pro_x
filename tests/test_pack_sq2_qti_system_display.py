"""Run the real pre-init stage against a temporary vendor tree and check archive tails."""
from pathlib import Path
import gzip
import hashlib
import importlib.util
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("qti_system_pack", ROOT / "scripts/pack-sq2-qti-system-display.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class SystemDisplayPack(unittest.TestCase):
    def fixture(self, temp, video_enabled=False):
        source, vendor = temp / "source", temp / "vendor"
        root = source / "root"
        root.mkdir(parents=True)
        (vendor / "lib64/egl").mkdir(parents=True)
        (vendor / "lib64/hw").mkdir()
        original = b"original allocator declaration\n"
        target = "etc/init/vendor.qti.hardware.display.allocator-service.rc"
        (vendor / target).parent.mkdir(parents=True)
        (vendor / target).write_bytes(original)
        (vendor / "lib64/egl/old-wrapper.so").write_bytes(b"unchanged old target")
        (vendor / "lib64/egl/libEGL_freedreno.so").symlink_to("old-wrapper.so")
        (vendor / "lib64/hw/camera.unrelated.so").write_bytes(b"unrelated HAL")
        (temp / "credentials").write_bytes(b"unrelated identity sentinel")
        for name in pack.OPTIONAL_EXTENSIONS:
            (vendor / "lib64" / name).write_bytes(b"unmatched C++ extension")
        for name in pack.LEGACY_GRAPHICS_ADAPTERS:
            (vendor / "lib64/egl" / name).write_bytes(b"retired synthetic adapter")
        (vendor / "lib64/android.hardware.graphics.mapper@3.0.so").write_bytes(b"retained HIDL interface")
        for name in pack.LEGACY_MAPPERS:
            (vendor / "lib64/hw" / name).write_bytes(b"old mapper implementation")
        manager_rc = b"service hwservicemanager /system_ext/bin/hwservicemanager\n    class core\n"
        video_rc = b"service sq2-omx /vendor/bin/hw/quest2-omx-service\n    disabled\n"
        hidl = {"hwservicemanager": b"real manager fixture",
                "framework-manifest.xml": b"<manifest><hal><name>android.hidl.manager</name></hal></manifest>",
                "original-video.rc": manager_rc + b"\n" + video_rc,
                "video-services.rc": video_rc}
        (source / "hidl-support").mkdir()
        for name, data in hidl.items():
            (source / "hidl-support" / name).write_bytes(data)
        system_ext = temp / "system_ext"
        (system_ext / "bin").mkdir(parents=True)
        (system_ext / "etc/vintf").mkdir(parents=True)
        (system_ext / "bin/unrelated").write_bytes(b"unrelated system executable")
        (system_ext / "etc/vintf/manifest.xml").write_bytes(hidl["framework-manifest.xml"] if video_enabled else b"<manifest/>")
        if video_enabled:
            (system_ext / "bin/hwservicemanager").write_bytes(hidl["hwservicemanager"])
            (vendor / "etc/init/quest2-hidl.rc").write_bytes(hidl["original-video.rc"])
        mockbin = temp / "mockbin"
        mockbin.mkdir()
        # Host fixture directories are already writable. File binds copy; overlay/bind-directory
        # calls are recorded and otherwise have no mount or root-namespace side effects.
        mount = mockbin / "mount"
        mount.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "' + str(temp / "mounts") + '"\n'
                         'if [ "$1" = --bind ] && [ -f "$2" ]; then cp "$2" "$3"; fi\n')
        mount.chmod(0o755)
        files = {target: (ROOT / "configs/sq2-qti-system-allocator.rc").read_bytes(),
                 "etc/init/sq2-qti-hwservicemanager.rc": manager_rc,
                 "lib64/egl/libEGL_adreno.so": b"native EGL fixture",
                 "lib64/egl/libGLESv1_CM_adreno.so": b"preserved GLES1 fixture",
                 "lib64/libgsl.so": b"native GSL fixture",
                 "lib64/mapper.qti.so": b"native extension companion, not a HAL",
                 "lib64/hw/gralloc.kona.so": b"native gralloc fixture"}
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        sums = "".join(sha(data) + "  root/" + name + "\n" for name, data in files.items())
        hidl_sums = "".join(sha(data) + "  hidl-support/" + name + "\n" for name, data in hidl.items())
        (source / "SHA256SUMS").write_text(sums + hidl_sums)
        (source / "INSTALLED_SHA256SUMS").write_text(sums.replace("  root/", "  "))
        (source / "ORIGINAL_SHA256SUMS").write_text(sha(original) + "  " + target + "\n")
        (source / "INSTALL_FILES").write_text("\n".join(files) + "\n")
        (source / "SYMLINKS").write_text("lib64/egl/libEGL_freedreno.so libEGL_adreno.so\n"
                                           "lib64/hw/gralloc.default.so gralloc.kona.so\n"
                                           "lib64/egl/libGLESv1_CM_freedreno.so libGLESv1_CM_adreno.so\n"
                                           "lib64/egl/libgsl.so ../libgsl.so\n")
        # Substitute only host fixture paths and its already-established overlay precondition.
        script = pack.stage_script().replace("source=/etc/sq2-qti-system", "source=" + str(source))
        script = script.replace("vendor=/newroot/vendor", "vendor=" + str(vendor))
        script = script.replace("system_ext=/newroot/system_ext", "system_ext=" + str(system_ext))
        script = script.replace("hidl_stage=/newroot/debug_ramdisk/sq2-qti-hidl-bin", "hidl_stage=" + str(temp / "hidl-stage"))
        script = script.replace("set -eu\n", "set -eu\nPATH=" + str(mockbin) + ":$PATH\nexport PATH\n")
        script = script.replace("grep -q '^overlay /newroot/vendor overlay ' /proc/mounts", 'test -d "$vendor"')
        runner = temp / "stage.sh"
        runner.write_text(script)
        return source, vendor, runner, files

    def test_real_stage_replaces_symlinks_and_only_selected_hal_files(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            _, vendor, runner, files = self.fixture(temp)
            result = subprocess.run(["sh", str(runner)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name, data in files.items():
                self.assertEqual((vendor / name).read_bytes(), data)
            alias = vendor / "lib64/egl/libEGL_freedreno.so"
            self.assertEqual(alias.readlink().as_posix(), "libEGL_adreno.so")
            self.assertEqual(alias.read_bytes(), files["lib64/egl/libEGL_adreno.so"])
            self.assertEqual((vendor / "lib64/egl/old-wrapper.so").read_bytes(), b"unchanged old target")
            for name in pack.OPTIONAL_EXTENSIONS:
                self.assertFalse((vendor / "lib64" / name).exists())
            for name in pack.LEGACY_MAPPERS:
                self.assertFalse((vendor / "lib64/hw" / name).exists())
            for name in pack.LEGACY_GRAPHICS_ADAPTERS:
                self.assertFalse((vendor / "lib64/egl" / name).exists())
            self.assertEqual((vendor / "lib64/egl/libGLESv1_CM_freedreno.so").read_bytes(), files["lib64/egl/libGLESv1_CM_adreno.so"])
            self.assertEqual((vendor / "lib64/egl/libgsl.so").read_bytes(), files["lib64/libgsl.so"])
            self.assertEqual((vendor / "lib64/android.hardware.graphics.mapper@3.0.so").read_bytes(), b"retained HIDL interface")
            self.assertEqual((vendor / "lib64/hw/camera.unrelated.so").read_bytes(), b"unrelated HAL")
            self.assertEqual((temp / "credentials").read_bytes(), b"unrelated identity sentinel")

    def test_original_contract_mismatch_exits_before_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            _, vendor, runner, _ = self.fixture(temp)
            original = vendor / "etc/init/vendor.qti.hardware.display.allocator-service.rc"
            original.write_bytes(b"unexpected current service")
            result = subprocess.run(["sh", str(runner)], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(original.read_bytes(), b"unexpected current service")
            self.assertEqual((vendor / "lib64/egl/libEGL_freedreno.so").readlink().as_posix(), "old-wrapper.so")
            self.assertFalse((vendor / "lib64/egl/libEGL_adreno.so").exists())
            self.assertTrue(all((vendor / "lib64/hw" / name).exists() for name in pack.LEGACY_MAPPERS))

    def test_hidl_manager_is_unique_with_video_enabled_or_disabled_and_on_reapply(self):
        for video_enabled in (False, True):
            with self.subTest(video_enabled=video_enabled), tempfile.TemporaryDirectory() as directory:
                temp = Path(directory)
                source, vendor, runner, _ = self.fixture(temp, video_enabled)
                initial_mounts = None
                for _ in range(2):
                    result = subprocess.run(["sh", str(runner)], text=True, capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    owners = sum(rc.read_text().count("service hwservicemanager ") for rc in (vendor / "etc/init").glob("*.rc"))
                    self.assertEqual(owners, 1)
                    self.assertEqual((temp / "system_ext/bin/hwservicemanager").read_bytes(),
                                     (source / "hidl-support/hwservicemanager").read_bytes())
                    self.assertEqual((temp / "system_ext/etc/vintf/manifest.xml").read_bytes(),
                                     (source / "hidl-support/framework-manifest.xml").read_bytes())
                    self.assertEqual((temp / "system_ext/bin/unrelated").read_bytes(), b"unrelated system executable")
                    video_rc = vendor / "etc/init/quest2-hidl.rc"
                    if video_enabled:
                        self.assertEqual(video_rc.read_bytes(), (source / "hidl-support/video-services.rc").read_bytes())
                    else:
                        self.assertFalse(video_rc.exists(), "video-disabled boot must not install a codec service")
                    mounts = (temp / "mounts").read_text() if (temp / "mounts").exists() else ""
                    if initial_mounts is None:
                        initial_mounts = mounts
                    else:
                        self.assertEqual(mounts, initial_mounts, "reapply must not stack another system_ext overlay")

    def test_codec2_dependency_patch_preserves_code_and_resolves_real_companion(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory)
            (p/"old.c").write_text("int state_query(void){return 5;}\n")
            (p/"companion.c").write_text("int state_query(void){return 7;} int extension_api(void){return 2;}\n")
            (p/"client.c").write_text("extern int state_query(void),extension_api(void); int query(void){return state_query()+extension_api();}\n")
            for source,name in [("old.c","libmapperutils.so"),("companion.c","mapper.qti.so")]:
                subprocess.run(["gcc","-fPIC","-shared",str(p/source),"-Wl,-soname,"+name,"-o",str(p/name)],check=True)
            original=p/"original.so"
            subprocess.run(["gcc","-fPIC","-shared",str(p/"client.c"),"-L",str(p),"-lmapperutils","-l:mapper.qti.so",
                            "-Wl,-rpath,$ORIGIN","-o",str(original)],check=True)
            before=original.read_bytes()
            with self.assertRaisesRegex(ValueError,"Unknown normal QTI Codec2 input"):
                pack.patch_codec2_mapper_dependency(original,"0"*64)
            patched=pack.patch_codec2_mapper_dependency(original,sha(before))
            self.assertEqual(original.read_bytes(),before)
            self.assertEqual(pack.executable_sections(before),pack.executable_sections(patched))
            (p/"patched.so").write_bytes(patched)
            for name,value in [("original.so",7),("patched.so",9)]:
                probe="import ctypes; assert ctypes.CDLL("+repr(str(p/name))+").query()=="+str(value)
                subprocess.run(["python3","-c",probe],check=True)
            needed=subprocess.check_output(["patchelf","--print-needed",str(p/"patched.so")],text=True).splitlines()
            self.assertNotIn("libmapperutils.so",needed)
            self.assertIn("mapper.qti.so",needed)

    def test_concatenated_archive_metadata_and_exact_private_tail(self):
        cpio = pack.load_module("pack-sq2-vulkan")
        def member(entries):
            data = b"".join(cpio.entry(name, body, mode) for name, mode, body in entries)
            return gzip.compress(data + cpio.entry("TRAILER!!!", b"", 0), mtime=0)
        original = member([("init", 0o100755, b"original"), ("kernel", 0o100600, b"kernel bytes")])
        private = member([("etc/shadow", 0o100600, b"test-only identity"), ("bin/dropbear", 0o100755, b"test-only executable")])
        update = member([("init", 0o100755, b"new hook")])
        base = original + private
        self.assertEqual(pack.credential_tail(base + update), private)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "archive.gz"
            path.write_bytes(base)
            before, _ = pack.scan(path)
            path.write_bytes(base + update + pack.credential_tail(base))
            after, bodies = pack.scan(path, {"init"})
            self.assertEqual(bodies["init"], b"new hook")
            for name in ("kernel", "etc/shadow", "bin/dropbear"):
                self.assertEqual(before[name], after[name], name)
            self.assertEqual(after["etc/shadow"][1][1], 0o100600)


if __name__ == "__main__":
    unittest.main()
