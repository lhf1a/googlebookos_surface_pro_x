#!/usr/bin/env python3
"""Extract and verify the pinned complete donor; external audio/Wi-Fi links are not followed."""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def prepare(archive, output):
    pins = json.loads((ROOT / "configs/kgsl-sde-source-inputs.json").read_text())
    with archive.open("rb") as stream:
        digest = hashlib.file_digest(stream, "sha256").hexdigest()
    if digest != pins["archive_sha256"] or archive.stat().st_size != pins["archive_bytes"]:
        raise ValueError("Donor archive mismatch")
    output.mkdir(parents=True, exist_ok=False)
    skipped = []
    def checked(member, destination):
        try:
            return tarfile.data_filter(member, destination)
        except tarfile.LinkOutsideDestinationError:
            if not ("/techpack/audio/" in member.name or "/drivers/net/wireless/broadcom/" in member.name):
                raise
            skipped.append(dict(path=member.name, target=member.linkname))
            return None
    with tarfile.open(archive) as source:
        source.extractall(output, filter=checked)
    kernel = output / ("oculus-linux-kernel-" + pins["commit"])
    for name, expected in pins["sha256"].items():
        path = kernel / name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            raise ValueError("Donor file mismatch: " + name)
    report = dict(status="source-verified-not-ported", archive_sha256=digest, commit=pins["commit"],
                  kernel=str(kernel.resolve()), verified_files=len(pins["sha256"]), external_links_not_extracted=skipped)
    (output / "SOURCE.json").write_text(json.dumps(report, indent=2) + "\n")
    return report

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.archive, args.output), indent=2))
