#!/usr/bin/env python3
"""Prepare the pinned 6.18.60 source tree with the adapted A6xx upstream batch.

Host-side source preparation only. The script verifies the pinned archive and
patch provenance, materializes a pristine tree from the archive, applies the
adapted 11-patch batch in strict config order and checks textual post-conditions
(baseline text of the pinned tree, absent-before preconditions, per-patch
post-conditions, final markers and final forbidden patterns). It never compiles a
kernel, never touches a device, and never modifies the pinned archive,
private/build-inputs or tracked repository files.

    --check     verify pins and rehearse the series in a temporary sparse tree
    --apply     extract the pinned kernel/ tree into a fresh work directory,
                apply the series, verify it and write <work-dir>/SOURCE.json

Apply failures remove the fresh work directory again, so nothing is left
half-applied. Defaults to configs/drm-618-a6xx-upstream-20261111.json.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

SHA256 = re.compile(r"[0-9a-f]{64}\Z")
APPLY_TOOLS = ("patch", "git")


def repo_root(start=None):
    """Locate the repository root (also works when this file lives in out/)."""
    for parent in Path(start or __file__).resolve().parents:
        if (parent / "configs" / "kernel-baseline.json").is_file():
            return parent
    raise ValueError("Cannot locate the repository root")


ROOT = repo_root()
DEFAULT_CONFIG = ROOT / "configs/drm-618-a6xx-upstream-20261111.json"


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def resolve(base, value):
    path = Path(value)
    return path if path.is_absolute() else (Path(base) / path)


def safe_relative(name):
    relative = PurePosixPath(name)
    return bool(relative.parts) and not relative.is_absolute() and ".." not in relative.parts


def load_config(path=DEFAULT_CONFIG):
    path = Path(path)
    require(path.is_file(), "Config not found: " + str(path))
    config = json.loads(path.read_text(encoding="utf-8"))
    validate_config(config)
    return config, path.resolve()


def validate_config(config):
    require(config.get("schema") == 1, "Config schema must be 1")
    require(config.get("kernel_version") == "6.18.60", "Config must pin kernel_version 6.18.60")
    source = config.get("source") or {}
    require(isinstance(source.get("archive"), str) and source["archive"], "Config source.archive is required")
    require(SHA256.match(source.get("archive_sha256", "")), "Config source.archive_sha256 is required")
    subdir = source.get("source_subdir", "kernel")
    require(isinstance(subdir, str) and safe_relative(subdir) and "/" not in subdir,
            "Config source.source_subdir must be one safe path component")
    patches = config.get("patches")
    require(isinstance(patches, list) and patches, "Config patches must be a non-empty list")
    for index, entry in enumerate(patches, 1):
        require(isinstance(entry.get("path"), str) and entry["path"], f"Patch {index}: path is required")
        require(SHA256.match(entry.get("sha256", "")), f"Patch {index}: sha256 is required")
        require(entry.get("role") in ("prerequisite", "fix"), f"Patch {index}: role must be prerequisite or fix")
        files = entry.get("files")
        require(isinstance(files, dict) and files, f"Patch {index}: files are required")
        for name, hashes in files.items():
            require(safe_relative(name), f"Patch {index}: unsafe source path {name}")
            require(name.startswith(subdir + "/"), f"Patch {index}: {name} is outside {subdir}/")
            require(isinstance(hashes, dict), f"Patch {index}: {name} needs before/after")
            require(hashes.get("before") is None or SHA256.match(hashes.get("before") or ""),
                    f"Patch {index}: {name} before hash is invalid")
            require(SHA256.match(hashes.get("after") or ""), f"Patch {index}: {name} after hash is invalid")
        for spec in entry.get("post_contains", []):
            _validate_spec(spec, f"Patch {index} post-condition", require_id=False)
    for spec in config.get("preconditions", []):
        require(isinstance(spec.get("absent"), str) and spec["absent"],
                "Precondition needs a non-empty absent marker")
        _validate_spec(spec, "Precondition")
    for spec in config.get("baseline", []):
        _validate_spec(spec, "Baseline marker")
    for spec in config.get("final_absent", []):
        require(isinstance(spec.get("absent"), str) or "regex" in spec,
                "Final forbidden needs a non-empty absent marker or a regex")
        _validate_spec(spec, "Final forbidden")
    markers = config.get("markers")
    require(isinstance(markers, list) and markers, "Config markers must be a non-empty list")
    for spec in markers:
        _validate_spec(spec, "Marker")
        require("contains" in spec or "regex" in spec, "Marker needs contains or regex")


def _validate_spec(spec, label, require_id=True):
    require(isinstance(spec, dict), label + " must be an object")
    if require_id:
        require(isinstance(spec.get("id"), str) and spec["id"], label + ": id is required")
    require(isinstance(spec.get("file"), str) and safe_relative(spec["file"]),
            label + ": safe file path is required")
    if "regex" in spec:
        require(isinstance(spec["regex"], str) and spec["regex"], label + ": empty regex")
    elif "contains" in spec:
        require(isinstance(spec["contains"], str) and spec["contains"],
                label + ": contains marker is empty")


def verify_series_config(config):
    """Reject a patch list whose per-file hash chain or marker targets disagree."""
    touched = {}
    for index, entry in enumerate(config["patches"], 1):
        for name, hashes in entry["files"].items():
            before, after = hashes["before"], hashes["after"]
            if before is None:
                require(name not in touched, f"Patch {index}: {name} is added twice")
            elif name in touched:
                require(before == touched[name],
                        f"Patch {index}: {name} before hash does not follow the previous patch")
            touched[name] = after
    produced = set(touched)
    for spec in config.get("markers", []):
        require(spec["file"] in produced, "Marker target was never patched: " + spec["file"])
    for spec in config.get("preconditions", []):
        require(spec["file"] in produced, "Precondition target was never patched: " + spec["file"])
    for spec in config.get("baseline", []):
        require(spec["file"] in produced, "Baseline target was never patched: " + spec["file"])
    for spec in config.get("final_absent", []):
        require(spec["file"] in produced, "Final forbidden target was never patched: " + spec["file"])
    roles = [entry["role"] for entry in config["patches"]]
    if "prerequisite" in roles:
        require(roles.index("prerequisite") == 0,
                "A prerequisite patch must run before the fixes it enables")
    return touched


def verify_archive(archive, expected_sha, expected_bytes=None):
    archive = Path(archive)
    require(archive.is_file(), "Source archive not found: " + str(archive))
    actual = digest(archive)
    require(actual == expected_sha, "Source archive sha256 mismatch: " + actual)
    if expected_bytes is not None:
        require(archive.stat().st_size == expected_bytes,
                "Source archive size mismatch: " + str(archive.stat().st_size))
    return actual


def verify_patch_files(config, config_root):
    entries = []
    for index, entry in enumerate(config["patches"], 1):
        path = resolve(config_root, entry["path"])
        require(path.is_file(), f"Patch {index} not found: {path}")
        actual = digest(path)
        require(actual == entry["sha256"], f"Patch {index} sha256 mismatch: {path}")
        entries.append({"index": index, "path": entry["path"], "sha256": actual,
                        "role": entry["role"], "subject": entry.get("subject")})
    return entries


def read_archive_files(archive, names):
    names = set(names)
    contents = {}
    with tarfile.open(archive) as tar:
        for member in tar:
            if member.name in names and member.isfile():
                extracted = tar.extractfile(member)
                require(extracted is not None, "Cannot read archive member: " + member.name)
                contents[member.name] = extracted.read()
                if len(contents) == len(names):
                    break
    missing = sorted(names - set(contents))
    require(not missing, "Source archive is missing expected files: " + ", ".join(missing))
    return contents


def materialize_sparse(archive, root, names):
    for name, payload in read_archive_files(archive, names).items():
        target = Path(root) / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload)


def extract_source_tree(archive, work_dir, source_subdir, extract_filter="data"):
    prefix = source_subdir + "/"
    count = 0
    with tarfile.open(archive) as tar:
        members = [member for member in tar if member.name == source_subdir or member.name.startswith(prefix)]
        require(members, "Source archive has no " + prefix + " tree")
        for member in members:
            require(safe_relative(member.name), "Unsafe archive member: " + member.name)
        try:
            tar.extractall(work_dir, members=members, filter=extract_filter)
        except TypeError:  # Python < 3.12 has no filter argument
            tar.extractall(work_dir, members=members)
        count = sum(1 for member in members if member.isfile())
    return count


def verify_kernel_version(tree, expected):
    makefile = Path(tree) / "Makefile"
    require(makefile.is_file(), "Extracted source has no " + str(makefile))
    fields = {}
    for match in re.finditer(r"^(VERSION|PATCHLEVEL|SUBLEVEL)\s*=\s*(\d+)\s*$",
                             makefile.read_text(encoding="utf-8", errors="replace"), re.M):
        fields[match.group(1)] = match.group(2)
    require(set(fields) == {"VERSION", "PATCHLEVEL", "SUBLEVEL"},
            "Kernel Makefile is missing a version field")
    actual = ".".join(fields[name] for name in ("VERSION", "PATCHLEVEL", "SUBLEVEL"))
    require(actual == expected, f"Unsupported kernel source {actual}; expected {expected}")
    return actual


def verify_present(root, spec, label):
    path = Path(root) / spec["file"]
    require(path.is_file(), f"{label}: missing {spec['file']}")
    text = path.read_text(encoding="utf-8", errors="replace")
    if "regex" in spec:
        require(re.search(spec["regex"], text) is not None,
                f"{label}: regex not found in {spec['file']}")
    else:
        expected = spec.get("count", 1)
        actual = text.count(spec["contains"])
        require(actual >= expected,
                f"{label}: expected {expected}x {spec['contains']!r} in {spec['file']}, found {actual}")


def verify_absent(root, spec, label):
    path = Path(root) / spec["file"]
    require(path.is_file(), f"{label}: missing {spec['file']}")
    text = path.read_text(encoding="utf-8", errors="replace")
    if "regex" in spec:
        require(re.search(spec["regex"], text) is None,
                f"{label}: {spec['file']} still matches the forbidden pattern")
    else:
        require(spec["absent"] not in text,
                f"{label}: {spec['file']} already contains {spec['absent']!r}")


def apply_patch(source_tree, patch, preferred="patch"):
    errors = []
    tools = [preferred] + [tool for tool in APPLY_TOOLS if tool != preferred]
    for tool in tools:
        if tool == "git":
            # git apply resolves patch paths against the enclosing repository root
            # and silently ignores paths outside the current directory; the extracted
            # tree can live below a work/ directory inside this repository. Refuse it
            # there instead of letting an exit-0 no-op look like a successful apply.
            top = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=source_tree,
                                 capture_output=True, text=True)
            if top.returncode == 0:
                errors.append("git: " + str(source_tree) + " is inside git worktree " + top.stdout.strip())
                continue
            check = ["git", "apply", "--check", "--whitespace=nowarn", str(patch)]
            command = ["git", "apply", "--whitespace=nowarn", str(patch)]
        else:
            check = ["patch", "--batch", "--forward", "--fuzz=0", "-p1", "--dry-run", "-i", str(patch)]
            command = ["patch", "--batch", "--forward", "--fuzz=0", "-p1", "-i", str(patch)]
        try:
            probe = subprocess.run(check, cwd=source_tree, capture_output=True, text=True)
            if probe.returncode != 0:
                raise ValueError((probe.stderr or probe.stdout).strip())
            applied = subprocess.run(command, cwd=source_tree, capture_output=True, text=True)
            if applied.returncode != 0:
                raise ValueError((applied.stderr or applied.stdout).strip())
            return tool
        except FileNotFoundError:
            errors.append(tool + ": not found")
        except ValueError as error:
            errors.append(tool + ": " + str(error))
    raise ValueError(f"Could not apply {Path(patch).name}: " + "; ".join(errors))


def _verify_files(work_dir, entry, stage, index):
    for name, hashes in entry["files"].items():
        path = Path(work_dir) / name
        expected = hashes.get(stage)
        if expected is None:
            require(stage == "before" and not path.exists() and not path.is_symlink(),
                    f"Patch {index}: added source already exists: {name}")
            continue
        require(path.is_file() and not path.is_symlink(), f"Patch {index}: missing source {name}")
        actual = digest(path)
        require(actual == expected, f"Patch {index}: unexpected {stage}-patch hash for {name}: {actual}")


def _apply_series(work_dir, config, config_root, tool="patch"):
    source_subdir = config["source"].get("source_subdir", "kernel")
    source_tree = Path(work_dir) / source_subdir
    tools = []
    for index, entry in enumerate(config["patches"], 1):
        _verify_files(work_dir, entry, "before", index)
        used = apply_patch(source_tree, resolve(config_root, entry["path"]), tool)
        tools.append(used)
        _verify_files(work_dir, entry, "after", index)
        for spec in entry.get("post_contains", []):
            verify_present(work_dir, spec, f"Patch {index} post-condition")
    for spec in config.get("markers", []):
        verify_present(work_dir, spec, "Final marker")
    for spec in config.get("final_absent", []):
        verify_absent(work_dir, spec, "Final forbidden")
    return tools


def _report(config, mode, archive, archive_sha, patch_entries, tools, version, work_dir):
    files = {}
    for entry in config["patches"]:
        for name, hashes in entry["files"].items():
            files.setdefault(name, {})["before"] = hashes["before"]
            files[name]["after"] = hashes["after"]
    return {
        "schema": 1,
        "mode": mode,
        "name": config.get("name"),
        "status": config.get("status"),
        "kernel_version": version,
        "host_only": True,
        "compiled": False,
        "hardware_validated": False,
        "source": {
            "archive": str(archive),
            "archive_sha256": archive_sha,
            "archive_bytes": Path(archive).stat().st_size,
            "source_subdir": config["source"].get("source_subdir", "kernel"),
        },
        "patches": patch_entries,
        "apply_tool": sorted(set(tools)),
        "files": files,
        "baseline": [spec["id"] for spec in config.get("baseline", [])],
        "preconditions": [spec["id"] for spec in config.get("preconditions", [])],
        "markers": [spec["id"] for spec in config.get("markers", [])],
        "final_absent": [spec["id"] for spec in config.get("final_absent", [])],
        "work_dir": str(work_dir) if work_dir else None,
        "limits": config.get("limits", []),
    }


def prepare(config_path=DEFAULT_CONFIG, source=None, work_dir=None, mode="check"):
    require(mode in ("check", "apply"), "Unsupported mode: " + str(mode))
    config, resolved_config = load_config(config_path)
    config_root = resolved_config.parents[1]
    verify_series_config(config)
    archive = Path(source).resolve() if source else resolve(ROOT, config["source"]["archive"])
    archive_sha = verify_archive(archive, config["source"]["archive_sha256"],
                                 config["source"].get("archive_bytes"))
    patch_entries = verify_patch_files(config, config_root)
    source_subdir = config["source"].get("source_subdir", "kernel")
    version = config["kernel_version"]
    tool = config.get("apply", {}).get("tool", "patch")
    if mode == "check":
        with tempfile.TemporaryDirectory(prefix="a6xx-618-check-") as temporary:
            root = Path(temporary)
            names = {name for entry in config["patches"] for name in entry["files"]}
            names.add(source_subdir + "/Makefile")
            materialize_sparse(archive, root, names)
            verify_kernel_version(root / source_subdir, version)
            for spec in config.get("baseline", []):
                verify_present(root, spec, "Baseline marker")
            for spec in config.get("preconditions", []):
                verify_absent(root, spec, "Precondition")
            tools = _apply_series(root, config, config_root, tool)
            report = _report(config, "check", archive, archive_sha, patch_entries, tools, version, None)
            report["temporary_tree"] = True
            return report
    require(work_dir is not None, "--apply requires --work-dir")
    work = Path(work_dir).resolve()
    require(not work.exists() and not work.is_symlink(), "Use a fresh work directory: " + str(work))
    work.mkdir(parents=True)
    try:
        extracted = extract_source_tree(archive, work, source_subdir,
                                        config["source"].get("extract_filter", "data"))
        require(extracted, "Source tree extraction produced no files")
        verify_kernel_version(work / source_subdir, version)
        for spec in config.get("baseline", []):
            verify_present(work, spec, "Baseline marker")
        for spec in config.get("preconditions", []):
            verify_absent(work, spec, "Precondition")
        tools = _apply_series(work, config, config_root, tool)
        report = _report(config, "apply", archive, archive_sha, patch_entries, tools, version, work)
        report["extracted_files"] = extracted
        manifest = work / "SOURCE.json"
        report["manifest"] = str(manifest)
        manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return report
    except BaseException:
        shutil.rmtree(work, ignore_errors=True)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--check", action="store_true",
                       help="verify pins and rehearse the series in a temporary sparse tree")
    group.add_argument("--apply", action="store_true",
                       help="extract into a fresh work directory and apply + verify the series")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG,
                        help="series config (default: configs/drm-618-a6xx-upstream-20261111.json)")
    parser.add_argument("--source", type=Path,
                        help="override the pinned source archive (sha256 must still match)")
    parser.add_argument("--work-dir", type=Path, help="fresh output directory for --apply")
    args = parser.parse_args(argv)
    if args.apply and args.work_dir is None:
        parser.error("--apply requires --work-dir")
    if args.check and args.work_dir is not None:
        parser.error("--work-dir is only used by --apply")
    try:
        report = prepare(args.config, source=args.source, work_dir=args.work_dir,
                         mode="apply" if args.apply else "check")
    except ValueError as error:
        print("error: " + str(error), file=sys.stderr)
        return 1
    if report["mode"] == "apply":
        print("apply OK:", report["work_dir"], "|", len(report["patches"]), "patches |",
              len(report["markers"]), "markers | kernel", report["kernel_version"])
    else:
        print("check OK:", len(report["patches"]), "patches |", len(report["markers"]),
              "markers | kernel", report["kernel_version"], "| tool", ",".join(report["apply_tool"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
