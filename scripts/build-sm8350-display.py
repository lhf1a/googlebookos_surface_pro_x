#!/usr/bin/env python3
"""Build a matched source QTI SDE/SDM library set; never install or start HWC."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def verify(root, relative, digest):
    name = PurePosixPath(relative)
    if name.is_absolute() or ".." in name.parts:
        raise ValueError("Invalid pinned path: " + relative)
    path = root / name
    if path.is_symlink() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("Input outside source root: " + relative)
    if sha(path) != digest:
        raise ValueError("Input hash mismatch: " + relative)
    return path

def sources(root, directory):
    text = (root / directory / "Android.bp").read_text()
    blocks = re.findall(r"\bsrcs:\s*\[([^]]*)\]", text)
    if len(blocks) != 1:
        raise ValueError("Expected one literal source list: " + directory)
    names = re.findall(r'"([^"]+)"', blocks[0])
    if not names or any(not n.endswith(".cpp") or "*" in n or ".." in PurePosixPath(n).parts
                        or PurePosixPath(n).is_absolute() for n in names):
        raise ValueError("Unsupported source list: " + directory)
    return [root / directory / name for name in names]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "references", "cc", "drm-headers", "drm-library", "platform", "output"):
        p.add_argument("--" + name, type=Path, required=True)
    p.add_argument("--dpu-plane-properties", action="store_true")
    p.add_argument("--dpu-capabilities", action="store_true")
    p.add_argument("--dpu-output-fence", action="store_true")
    p.add_argument("--hwc-allocator-mapper5", action="store_true")
    p.add_argument("--dpu-sdm-contract", action="store_true")
    p.add_argument("--drm-device-lifecycle", action="store_true")
    p.add_argument("--dpu-display-topology", action="store_true")
    p.add_argument("--dpu-events", action="store_true")
    p.add_argument("--sde-mode-request", action="store_true")
    a = p.parse_args()
    if a.dpu_display_topology or a.dpu_events:
        a.dpu_sdm_contract = True
    if a.dpu_events:
        a.drm_device_lifecycle = True
    if a.dpu_sdm_contract:
        a.dpu_capabilities = a.dpu_output_fence = a.hwc_allocator_mapper5 = True
    manifest_path = ROOT / "configs/sm8350-display-build.json"
    config = json.loads(manifest_path.read_text())
    inputs = {}
    for relative, digest in config["source_files"].items():
        path = verify(a.source, relative, digest)
        inputs[str(path)] = digest
    for item in config["dependency_files"]:
        path = verify(a.references, item["path"], item["sha256"])
        inputs[str(path)] = item["sha256"]
    donor_pin = json.loads((ROOT / "configs/quest2-v77-display-reference.json").read_text())
    if sha(a.drm_library) != donor_pin["files"]["/lib64/libdrm.so"]:
        raise ValueError("Unexpected libdrm input")
    adaptations = []
    if a.dpu_plane_properties or a.dpu_capabilities or a.dpu_output_fence:
        adaptations.append(config["dpu_plane_properties"])
    if a.dpu_capabilities:
        adaptations.append(config["dpu_capabilities"])
    if a.dpu_output_fence:
        adaptations.append(config["dpu_output_fence"])
    if a.hwc_allocator_mapper5:
        adaptations.append(config["hwc_allocator_mapper5"])
    if a.dpu_sdm_contract:
        adaptations.append(config["dpu_sdm_contract"])
    for key in ("drm_device_lifecycle", "dpu_display_topology", "dpu_events"):
        if getattr(a, key):
            adaptations.append(config[key])
    if a.dpu_events:
        adaptations.append(config["dpu_optional_panel"])
        adaptations.append(config["dpu_validation_target"])
        adaptations.append(config["dpu_validation_phases"])
        adaptations.append(config["dpu_histogram_capability"])
        adaptations.append(config["dpu_flush_fence"])
        adaptations.append(config["dpu_native_overlay"])
        adaptations.append(config["dpu_overlay_composition"])
        adaptations.append(config["dpu_inherited_planes"])
        adaptations.append(config["dpu_sdr_color"])
        adaptations.append(config["dpu_busy_commit"])
    if a.sde_mode_request:
        if any((a.dpu_plane_properties, a.dpu_capabilities, a.dpu_output_fence,
                a.hwc_allocator_mapper5, a.dpu_sdm_contract, a.drm_device_lifecycle,
                a.dpu_display_topology, a.dpu_events)):
            raise ValueError("SDE mode request staging is not the mainline DPU backend")
        adaptations.append(config["sde_mode_request"])
    for adaptation in adaptations:
        for item in adaptation.get("headers", []):
            path = verify(ROOT, item["path"], item["sha256"])
            inputs[str(path)] = sha(path)
        for key in ("patch", "header"):
            if key not in adaptation:
                continue
            path = verify(ROOT, adaptation[key], adaptation[key + "_sha256"])
            inputs[str(path)] = sha(path)
    a.output.mkdir(parents=True, exist_ok=False)
    patch_commands = []
    if adaptations:
        stage = a.output / "source"
        for relative in config["source_files"]:
            target = stage / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(a.source / relative, target)
        for adaptation in adaptations:
            for relative, digest in adaptation.get("base_sources", {}).items():
                verify(stage, relative, digest)
            patch_command = ["patch", "--batch", "--fuzz=0", "-p1", "-i",
                             str(ROOT / adaptation["patch"])]
            subprocess.run(patch_command, cwd=stage, check=True)
            patch_commands.append({"cwd": str(stage), "argv": patch_command})
            for relative, digest in adaptation["patched_sources"].items():
                verify(stage, relative, digest)
        a.source = stage
    module_sources = {name: sources(a.source, group["directory"])
                      for name, group in config["modules"].items()}
    (a.output / "libdrm.so").write_bytes(a.drm_library.read_bytes())
    flags = [str(a.cc), "--target=aarch64-linux-android35", "-std=c++17", "-O2",
             "-fPIC", "-nostdlib++", "-fno-operator-names", "-Wall", "-Wextra", "-Werror",
             "-Wno-unused-parameter", "-Wno-missing-field-initializers",
             "-Wno-c99-designator", "-Wno-unqualified-std-cast-call",
             "-DPP_DRM_ENABLE", "-include", str(ROOT / "probe/graphics/quest2-platform-cxx.h")]
    for path in (a.source / "include", a.source / "libdebug", a.source / "libdrmutils",
                 a.source / "sde-drm", a.source / "sdm/include", a.source / "sdm/libs/core",
                 a.source / "sdm/libs/core/drm", a.references / "kernel-uapi",
                 a.references / "kernel-uapi/drm", a.drm_headers, a.drm_headers / "include",
                 a.drm_headers / "include/drm",
                 a.references / "android_vendor_qcom_opensource_display-commonsys-intf/include"):
        flags += ["-I", str(path)]
    if adaptations:
        flags += ["-I", str(ROOT / "probe/graphics")]
    jobs, objects, commands = [], {}, []
    for name, files in module_sources.items():
        objects[name] = []
        for index, source in enumerate(files):
            obj = a.output / f"{name}-{index}.o"
            dep = a.output / f"{name}-{index}.d"
            command = flags[:]
            if a.drm_device_lifecycle and source.name == "drm_master.cpp":
                # CLOSEFB is standard modern UAPI; retain downstream sde_drm.h for legacy RMFB2.
                command[1:1] = ["-I", str(a.drm_headers / "include/drm")]
            # Upstream uses all_of only to delete elements; its boolean result is intentionally unused.
            if source.name == "color_manager.cpp":
                command += ["-Wno-unused-result"]
            command += ["-MD", "-MF", str(dep), "-c", str(source), "-o", str(obj)]
            jobs.append((command, dep))
            objects[name].append(str(obj))
    def compile_one(job):
        command, dep = job
        result = subprocess.run(command, text=True, capture_output=True)
        dep.with_suffix(".log").write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(shlex.join(command) + "\n" + result.stderr)
        return command, shlex.split(dep.read_text().split(":", 1)[1].replace("\\\n", " "))
    # Independent translation units only; link in dependency order.
    with ThreadPoolExecutor(max_workers=6) as pool:
        for command, deps in pool.map(compile_one, jobs):
            commands.append(command)
            for dep in deps:
                inputs[str(Path(dep).resolve())] = sha(Path(dep))
    for name, group in config["modules"].items():
        command = [str(a.cc), "--target=aarch64-linux-android35", "-nostdlib++", "-shared",
                   "-Wl,--no-undefined", "-Wl,-soname,lib" + name + ".so"] + objects[name]
        command += ["-L", str(a.output), "-L", str(a.platform), "-lc++", "-ldl"]
        command += ["-l" + lib for lib in group["libraries"]]
        command += ["-o", str(a.output / ("lib" + name + ".so"))]
        commands.append(command)
        subprocess.run(command, check=True)
    for path in (a.cc.resolve(), a.cc.with_name("clang").resolve(), a.drm_library, a.platform / "libc++.so",
                 manifest_path, ROOT / "configs/quest2-v77-display-reference.json", Path(__file__)):
        inputs[str(path)] = sha(path)
    build = {"status": "source-sde-sdm-libraries-only", "deployed": False,
             "source_commit": config["source_commit"], "modules": list(config["modules"]),
             "translation_units": len(jobs), "commands": commands, "inputs": inputs,
             "dpu_plane_properties": a.dpu_plane_properties or a.dpu_capabilities or a.dpu_output_fence,
             "dpu_capabilities": a.dpu_capabilities, "dpu_output_fence": a.dpu_output_fence,
             "hwc_allocator_mapper5": a.hwc_allocator_mapper5,
             "dpu_sdm_contract": a.dpu_sdm_contract,
             "drm_device_lifecycle": a.drm_device_lifecycle,
             "dpu_display_topology": a.dpu_display_topology, "dpu_events": a.dpu_events,
             "dpu_optional_panel": a.dpu_events, "dpu_validation_target": a.dpu_events,
             "dpu_validation_phases": a.dpu_events,
             "dpu_histogram_capability": a.dpu_events, "dpu_flush_fence": a.dpu_events,
             "dpu_native_overlay": a.dpu_events,
             "dpu_overlay_composition": a.dpu_events,
             "patch_commands": patch_commands,
             "dpu_inherited_planes": a.dpu_events,
              "dpu_sdr_color": a.dpu_events,
             "dpu_busy_commit": a.dpu_events,
             "sha256": {path.name: sha(path) for path in list(a.output.glob("*.so"))},
             "limitations": config["limitations"]}
    (a.output / "BUILD.json").write_text(json.dumps(build, indent=2) + "\n")
    print(json.dumps({k: build[k] for k in ("status", "translation_units", "sha256")}))

if __name__ == "__main__":
    main()
