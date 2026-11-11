#!/usr/bin/env python3
"""Compile real SDE public-type use sites with actual target DRM headers.

These are narrowed header-completeness probes, NOT complete source TUs or a
module/hardware check. Source expressions and direct includes are extracted;
no replacement public structs or compatibility umbrella headers are supplied.
The explicitly supplied KOUT must be private or held exclusively by the caller.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

SITES = {
    "sde/sde_crtc.c": {
        "headers": ["drm/drm_framebuffer.h", "drm/drm_fourcc.h"],
        "expressions": ["fb->base.id", "fb->modifier", "fb->format->format",
                        "fb->format->cpp[i]", "fb->width", "fb->height",
                        "fb->pitches[i]", "fb->offsets[i]"]},
    "sde/sde_plane.c": {
        "headers": ["drm/drm_framebuffer.h", "drm/drm_fourcc.h"],
        "expressions": ["fb->base.id", "fb->format->format",
                        "fb->format->num_planes", "fb->height", "fb->modifier",
                        "fb->obj[i]", "fb->offsets", "fb->pitches", "fb->width"]},
    "sde/sde_encoder.c": {
        "headers": ["drm/drm_framebuffer.h"],
        "expressions": ["fb->height", "fb->width"]},
    "sde/sde_formats.c": {
        "headers": ["drm/drm_framebuffer.h"],
        "expressions": ["fb->height", "fb->pitches[i]", "fb->width"]},
    "msm_drv.c": {
        "headers": ["drm/drm_framebuffer.h"], "expressions": ["fb->filp_head"]},
    "dsi/dsi_display.h": {
        "headers": ["drm/drm_bridge.h"],
        "declaration": "struct drm_bridge_funcs bridge_funcs;"},
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--kernel-source", required=True, type=Path)
    parser.add_argument("--kernel-build", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="fresh result directory; never overwritten")
    parser.add_argument("--arch", default="arm64")
    parser.add_argument("--cross-compile", default="aarch64-linux-gnu-")
    parser.add_argument("--jobs", default=2, type=int)
    parser.add_argument("--timeout", default=180, type=int)
    args = parser.parse_args()
    source = args.source.resolve()
    msm = source / "techpack/display/msm"
    if not msm.is_dir():
        msm = source
    kernel, kout, output = (p.resolve() for p in
                            (args.kernel_source, args.kernel_build, args.output))
    output.mkdir(parents=True, exist_ok=False)
    probes = output / "probes"
    probes.mkdir()
    inputs, cases, targets = {}, [], []
    for index, (relative, site) in enumerate(SITES.items()):
        path = msm / relative
        text = path.read_text(encoding="utf-8")
        inputs[str(path)] = sha(path)
        # Intentionally isolate direct public-type dependencies from unrelated
        # donor headers/APIs. The complete source TUs are a separate check.
        direct = re.findall(r"^\s*#include\s*<([^>]+)>\s*$", text, re.M)
        relevant = [h for h in direct if h in
                    ("drm/drm_framebuffer.h", "drm/drm_fourcc.h", "drm/drm_bridge.h")]
        missing = [h for h in site["headers"] if h not in direct]
        for expression in site.get("expressions", []):
            if expression not in text:
                raise ValueError(f"actual use site changed: {relative}: {expression}")
        declaration = site.get("declaration")
        if declaration and declaration not in text:
            raise ValueError(f"actual field declaration changed: {relative}")
        name = f"sde_public_headers_{index}"
        generated = "#include <linux/module.h>\n"
        generated += "".join(f"#include <{h}>\n" for h in relevant)
        generated += "struct drm_framebuffer;\nstruct drm_bridge_funcs;\n"
        generated += "static __maybe_unused void probe(struct drm_framebuffer *fb, unsigned int i)\n{\n (void)fb; (void)i;\n"
        if declaration:
            generated += " " + declaration + "\n (void)sizeof(bridge_funcs);\n"
        generated += "".join(f" (void)({expression});\n" for expression in site.get("expressions", []))
        generated += "}\n"
        probe = probes / (name + ".c")
        probe.write_text(generated, encoding="utf-8")
        targets.append(name + ".o")
        cases.append(dict(source=str(path), source_sha256=sha(path),
                          direct_headers=relevant, missing_direct_headers=missing,
                          actual_expressions=site.get("expressions", []),
                          actual_declaration=declaration, probe=str(probe),
                          probe_sha256=sha(probe), object=str(probes / (name + ".o"))))
    (probes / "Kbuild").write_text("obj-m += " + " ".join(targets) + "\n")
    for relative in ("include/drm/drm_framebuffer.h", "include/drm/drm_fourcc.h",
                     "include/drm/drm_bridge.h"):
        inputs[str(kernel / relative)] = sha(kernel / relative)
    for relative in (".config", "include/config/auto.conf", "include/generated/autoconf.h"):
        inputs[str(kout / relative)] = sha(kout / relative)
    command = ["make", "-C", str(kernel), "O=" + str(kout), "M=" + str(probes),
               "ARCH=" + args.arch, "CROSS_COMPILE=" + args.cross_compile,
               "KCFLAGS=-Werror", "V=1", "-k", "-j" + str(args.jobs), *targets]
    process = subprocess.run(command, capture_output=True, text=True,
                             timeout=args.timeout,
                             env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
    (output / "compile.stdout.txt").write_text(process.stdout)
    (output / "compile.stderr.txt").write_text(process.stderr)
    for case in cases:
        case["object_built"] = Path(case["object"]).is_file()
        case["pass"] = case["object_built"] and not case["missing_direct_headers"]
    passed = sum(case["pass"] for case in cases)
    result = dict(status="pass" if passed == len(cases) and process.returncode == 0 else "fail",
                  command=command, stdout=process.stdout, stderr=process.stderr,
                  exit=process.returncode, inputs=inputs, cases=cases,
                  counts=dict(green=passed, red=len(cases) - passed),
                  checker_sha256=sha(__file__), full_translation_unit=False,
                  hardware_validated=False, fake_public_structs=False,
                  boundary="Actual use expressions/direct includes plus real Linux DRM headers; "
                           "header-completeness probes only, no full SDE/module/runtime claim.")
    (output / "RESULT.json").write_text(json.dumps(result, indent=2) + "\n")
    print(f"RESULT {passed} green {len(cases)-passed} red compiler_exit={process.returncode}")
    print(output / "RESULT.json")
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
