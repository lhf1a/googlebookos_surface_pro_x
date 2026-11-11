#!/usr/bin/env python3
"""Rebuild isolated boot-stage sources/helper. Never approve, stage or reboot.

This is NOT a complete boot payload builder. It preserves default source bytes
and does not supply the unresolved hardware contracts or a production receipt.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()


def apply_post_prelude_patches(output, patches, ledger):
    """Apply pinned increments only to their exact generated parent files.

    Plans seal modules and kernel notes, not init bytes. Do not rewrite a plan
    or its immutable input init to make an increment appear to be its parent.
    The outer BOOT-CANDIDATE manifest seals the final composed source instead.
    """
    def snapshot():
        entries = {}
        for p in output.rglob("*"):
            if p.is_symlink():
                raise ValueError("Unexpected source symlink: " + str(p))
            if p.is_file():
                entries[p.relative_to(output).as_posix()] = sha(p)
            elif not p.is_dir():
                raise ValueError("Unsupported source entry: " + str(p))
        return entries

    for item in patches:
        before, after = item["before"], item["after"]
        if not before or set(before) != set(after):
            raise ValueError("Post-prelude increment needs matching before/after files")
        original = snapshot()
        for name, digest in before.items():
            path = PurePosixPath(name)
            if path.is_absolute() or ".." in path.parts or path.as_posix() != name:
                raise ValueError("Invalid increment path: " + name)
            if original.get(name) != digest:
                raise ValueError("Post-prelude parent mismatch: " + name)
        command = ["patch", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / item["path"])]
        r = subprocess.run(command, cwd=output, capture_output=True, text=True)
        ledger.append(dict(command=command, cwd=str(output), exit=r.returncode, stdout=r.stdout, stderr=r.stderr))
        (output / "BUILD-LEDGER.json").write_text(json.dumps(ledger, indent=2) + "\n")
        if r.returncode:
            raise RuntimeError("Post-prelude patch failed; preserve BUILD-LEDGER.json")
        observed = snapshot()
        original.pop("BUILD-LEDGER.json", None)
        observed.pop("BUILD-LEDGER.json", None)
        wanted = dict(original)
        wanted.update(after)
        if observed != wanted:
            raise ValueError("Post-prelude patch changed undeclared files or wrong bytes")
        for name in after:
            r = subprocess.run(["sh", "-n", str(output / name)], capture_output=True, text=True)
            ledger.append(dict(command=["sh", "-n", str(output / name)], cwd=str(ROOT),
                               exit=r.returncode, stdout=r.stdout, stderr=r.stderr))
            if r.returncode:
                (output / "BUILD-LEDGER.json").write_text(json.dumps(ledger, indent=2) + "\n")
                raise RuntimeError("Post-prelude shell syntax failed")
        (output / "BUILD-LEDGER.json").write_text(json.dumps(ledger, indent=2) + "\n")


def prepare(output, prelude=None, runtime_firmware=None):
    output = output.absolute()
    if output.exists():
        raise ValueError("Use a fresh output directory")
    flow = json.loads((ROOT / "configs/kgsl-sde-boot-flow.json").read_text())
    candidate = flow["candidate"]
    patches = candidate.get("patches", [dict(path=candidate["patch"], sha256=candidate["patch_sha256"])])
    post_patches = candidate.get("post_prelude_patches", []) if prelude else []
    expected = {ROOT / n: digest for n, digest in candidate["parent_files"].items()}
    for item in patches + post_patches:
        expected[ROOT / item["path"]] = item["sha256"]
    expected[ROOT / candidate["helper_source"]] = candidate["helper_sha256"]
    if prelude:
        tool = flow["module_prelude"]
        expected[ROOT / tool["generator"]] = tool["generator_sha256"]
        expected[ROOT / tool["template"]] = tool["template_sha256"]
    for path, digest in expected.items():
        if path.is_symlink() or not path.resolve().is_relative_to(ROOT) or sha(path) != digest:
            raise ValueError("Unrecognized source input: " + str(path))
    output.mkdir(parents=True, exist_ok=False)
    (output / "bin").mkdir()
    (output / "src").mkdir()
    shutil.copy2(ROOT / "probe/test2/init-googlebook-native", output / "init")
    shutil.copy2(ROOT / "probe/test2/prepare-native-gpu", output / "bin/prepare-native-gpu")
    commands = [(["patch", "--batch", "--fuzz=0", "-p1", "-i", str(ROOT / item["path"])], output) for item in patches]
    commands += [
        (["sh", "-n", str(output / "init")], ROOT),
        (["sh", "-n", str(output / "bin/prepare-native-gpu")], ROOT),
    ]
    shutil.copy2(ROOT / candidate["helper_source"], output / "src/sq2-fdt-contract.c")
    commands.append((["aarch64-linux-gnu-gcc", "-std=c11", "-Os", "-static", "-Wall", "-Wextra", "-Werror",
                      str(output / "src/sq2-fdt-contract.c"), "-o", str(output / "bin/sq2-fdt-contract")], ROOT))
    ledger = []
    for command, cwd in commands:
        r = subprocess.run(command, cwd=cwd, capture_output=True, text=True)
        ledger.append(dict(command=command, cwd=str(cwd), exit=r.returncode, stdout=r.stdout, stderr=r.stderr))
        (output / "BUILD-LEDGER.json").write_text(json.dumps(ledger, indent=2) + "\n")
        if r.returncode:
            raise RuntimeError("Build failed; preserve BUILD-LEDGER.json: " + str(output))
    if prelude:
        # Preserve the exact parent rather than making PLAN.json point to the
        # subsequently overlaid init. The input remains independently reopenable.
        parent = output / "src/init-before-module-prelude"
        shutil.copy2(output / "init", parent)
        generated = output / "module-prelude-source"
        command = ["python3", str(ROOT / flow["module_prelude"]["generator"]),
                   "--init", str(parent), "--output", str(generated)]
        for key, value in prelude.items():
            command += ["--" + key, str(value.resolve())]
        r = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
        ledger.append(dict(command=command, cwd=str(ROOT), exit=r.returncode, stdout=r.stdout, stderr=r.stderr))
        (output / "BUILD-LEDGER.json").write_text(json.dumps(ledger, indent=2) + "\n")
        if r.returncode:
            raise RuntimeError("Prelude construction failed; preserve BUILD-LEDGER.json: " + str(output))
        shutil.copy2(generated / "init", output / "init")
        shutil.copy2(generated / "bin/sq2-module-prelude", output / "bin/sq2-module-prelude")
        shutil.copytree(generated / "etc", output / "etc")
        apply_post_prelude_patches(output, post_patches, ledger)
    if prelude:
        # A full runtime manifest also covers depmod metadata, not just .ko.
        modules = prelude["modules"].resolve()
        inventory = []
        for p in sorted(modules.rglob("*")):
            if p.is_symlink():
                raise ValueError("Normalize copied runtime module links first: " + str(p))
            if p.parent == modules and p.name in ("modules.dep.bb", "modules.dep.bb.new"):
                raise ValueError("Remove derived BusyBox cache from the runtime COPY first")
            if p.is_file():
                inventory.append(sha(p) + "  ./" + p.relative_to(modules).as_posix() + "\n")
            elif not p.is_dir():
                raise ValueError("Unsupported module entry: " + str(p))
        data = output / "input-inventories"
        data.mkdir(exist_ok=True)
        (data / "modules.sha256").write_text("".join(inventory))
        (data / "STATUS.txt").write_text("BLOCKED: inventories only; no production approval.\n")
    if runtime_firmware:
        # Inventory only: private firmware is not copied into this source bundle
        # and these files are NOT an APPROVED receipt.
        firmware = runtime_firmware.resolve()
        if not firmware.is_dir():
            raise ValueError("Runtime firmware directory missing")
        inventory = []
        for p in sorted(firmware.rglob("*")):
            if p.is_symlink():
                raise ValueError("Normalize firmware aliases on a copy first: " + str(p))
            if p.is_file():
                inventory.append(sha(p) + "  ./" + p.relative_to(firmware).as_posix() + "\n")
            elif not p.is_dir():
                raise ValueError("Unsupported firmware entry: " + str(p))
        if not inventory:
            raise ValueError("Empty firmware inventory")
        data = output / "input-inventories"
        data.mkdir(exist_ok=True)
        (data / "firmware.sha256").write_text("".join(inventory))
        (data / "STATUS.txt").write_text("BLOCKED: inventory only; no production approval.\n")
    for path, digest in expected.items():
        assert sha(path) == digest, "Original changed during construction"
    files = {p.relative_to(output).as_posix(): sha(p) for p in output.rglob("*") if p.is_file()}
    manifest = dict(status="HOST-CANDIDATE-ONLY-NOT-ACTIVATABLE", approval_generated=False,
                    module_prelude_attached=bool(prelude),
                    post_prelude_patches_attached=bool(post_patches),
                    firmware_inventory_attached=bool(runtime_firmware),
                    unresolved=flow["host_blockers"], sha256=files,
                    scope="Startup source mechanism and static helper only; not whole initramfs or hardware acceptance")
    (output / "BOOT-CANDIDATE.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--runtime-firmware", type=Path, help="Normalized local firmware tree; inventory only, never approval")
    for flag in ("modules", "kernel-config", "kernel-release", "kernel-notes", "busybox", "net-makefile"):
        parser.add_argument("--" + flag, type=Path)
    args = parser.parse_args()
    values = {"modules": args.modules, "config": args.kernel_config, "release": args.kernel_release,
              "kernel-notes": args.kernel_notes, "busybox": args.busybox, "net-makefile": args.net_makefile}
    if any(values.values()) and not all(values.values()):
        parser.error("Prelude inputs must be supplied together")
    result = prepare(args.output, values if all(values.values()) else None, args.runtime_firmware)
    print(result["status"], args.output.absolute())
