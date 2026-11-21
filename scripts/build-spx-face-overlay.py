#!/usr/bin/env python3
"""Build the static Settings RRO overlay so spx-face-hal drives centered IR preview."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OVERLAY_SRC = ROOT / "probe/face/overlay"
DEFAULT_SDK = Path(
    os.environ.get(
        "SQ2_ANDROID_SDK",
        "/mnt/spx-workspace/toolchains/sq2-graphics/sdk",
    )
)


def sdk_tools_available(sdk: Path = DEFAULT_SDK) -> bool:
    tools = Path(sdk) / "android-15"
    platform = Path(sdk) / "android-35/android.jar"
    return (
        (tools / "aapt2").is_file()
        and (tools / "zipalign").is_file()
        and (tools / "apksigner").is_file()
        and platform.is_file()
        and shutil.which("keytool") is not None
    )


def build_overlay(
    output: Path,
    sdk: Path = DEFAULT_SDK,
    keystore: Path | None = None,
    ephemeral_keystore: bool = False,
) -> Path:
    sdk = Path(sdk).resolve()
    tools = sdk / "android-15"
    platform_jar = sdk / "android-35/android.jar"
    out = Path(output).resolve()
    out.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="spx-face-overlay-") as tmp:
        tmp_dir = Path(tmp)
        if ephemeral_keystore:
            key = tmp_dir / "ephemeral.keystore"
        else:
            key = (keystore or ROOT / "private/spx-face/overlay.keystore").resolve()
            key.parent.mkdir(parents=True, exist_ok=True)

        if not key.exists():
            subprocess.run(
                [
                    "keytool",
                    "-genkeypair",
                    "-keystore",
                    str(key),
                    "-storepass",
                    "android",
                    "-keypass",
                    "android",
                    "-alias",
                    "spx-face",
                    "-dname",
                    "CN=Local Surface Face Overlay",
                    "-keyalg",
                    "RSA",
                    "-keysize",
                    "2048",
                    "-validity",
                    "10000",
                ],
                check=True,
                capture_output=True,
            )

        res_zip = tmp_dir / "resources.zip"
        unsigned_apk = tmp_dir / "unsigned.apk"
        aligned_apk = tmp_dir / "aligned.apk"
        apk_path = out / "spx-face-overlay.apk"

        subprocess.run(
            [str(tools / "aapt2"), "compile", "--dir", str(OVERLAY_SRC / "res"), "-o", str(res_zip)],
            check=True,
        )
        subprocess.run(
            [
                str(tools / "aapt2"),
                "link",
                "--manifest",
                str(OVERLAY_SRC / "AndroidManifest.xml"),
                "-I",
                str(platform_jar),
                "--auto-add-overlay",
                "-o",
                str(unsigned_apk),
                str(res_zip),
            ],
            check=True,
        )
        subprocess.run(
            [str(tools / "zipalign"), "-f", "-P", "16", "4", str(unsigned_apk), str(aligned_apk)],
            check=True,
        )
        subprocess.run(
            [
                str(tools / "apksigner"),
                "sign",
                "--ks",
                str(key),
                "--ks-pass",
                "pass:android",
                "--key-pass",
                "pass:android",
                "--out",
                str(apk_path),
                str(aligned_apk),
            ],
            check=True,
        )
        subprocess.run([str(tools / "apksigner"), "verify", str(apk_path)], check=True)
        return apk_path


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sdk", type=Path, default=DEFAULT_SDK)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--keystore", type=Path, help="explicit local build signing key; never packaged")
    p.add_argument(
        "--ephemeral-keystore",
        action="store_true",
        help="sign with a temporary key inside a temp dir so workspace remains clean",
    )
    a = p.parse_args()
    apk = build_overlay(a.output, sdk=a.sdk, keystore=a.keystore, ephemeral_keystore=a.ephemeral_keystore)
    print(f"Built {apk}")


if __name__ == "__main__":
    main()
