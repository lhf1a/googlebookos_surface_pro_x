"""Default Surface Pro X GNSS AIDL V4 HAL integration into initramfs."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile

from disk_image import require

ROOT = Path(__file__).resolve().parents[1]
GNSS_DIR = ROOT / "probe/gnss"

HAL_TARGET = "etc/spx-gnss/android.hardware.gnss-service.example"
MANIFEST_TARGET = "etc/spx-gnss/android.hardware.gnss-service.spx.xml"
RC_TARGET = "etc/spx-gnss/android.hardware.gnss-service.spx.rc"
STAGE_TARGET = "bin/spx-gnss-stage"

STAGE = b"qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n"
STAGE_HOOK = b"spx-gnss-stage > /logs/gnss-stage.txt 2>&1 || echo SPX_GNSS_STAGE_FAILED\n"


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _verify_arm64_elf(data: bytes, label: str) -> None:
    require(
        len(data) >= 64
        and data[:4] == b"\x7fELF"
        and data[4] == 2
        and data[5] == 1
        and int.from_bytes(data[18:20], "little") == 183,
        f"{label} is not an ARM64 ELF64 binary",
    )


def _hal_builder():
    spec = importlib.util.spec_from_file_location(
        "build_spx_gnss_hal", ROOT / "scripts/build-spx-gnss-hal.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def toolchains_available() -> bool:
    return _hal_builder().toolchains_available()


def build_default_hal() -> bytes:
    mod = _hal_builder()
    with tempfile.TemporaryDirectory(prefix="spx-gnss-hal-") as tmp:
        meta = mod.build_hal(Path(tmp))
        return Path(meta["binary"]).read_bytes()


def load(gnss_hal_path: Path | None = None, compiled_hal: bytes | None = None) -> bytes | None:
    if compiled_hal is not None:
        _verify_arm64_elf(compiled_hal, HAL_TARGET)
        return compiled_hal
    if gnss_hal_path is not None:
        data = Path(gnss_hal_path).read_bytes()
        _verify_arm64_elf(data, str(gnss_hal_path))
        return data
    if toolchains_available():
        data = build_default_hal()
        _verify_arm64_elf(data, HAL_TARGET)
        return data
    return None


def install(records, bodies, hal_bytes: bytes, new_record) -> dict:
    _verify_arm64_elf(hal_bytes, HAL_TARGET)
    init = bodies["init"]
    if b"spx-gnss-stage >" not in init:
        require(init.count(STAGE) == 1, "GNSS requires QTI display stage anchor in init")
        init = init.replace(STAGE, STAGE + STAGE_HOOK)
    bodies["init"] = init

    bodies[HAL_TARGET] = hal_bytes
    records[HAL_TARGET] = new_record(0o100755)
    for target, src_name, mode in (
        (MANIFEST_TARGET, "android.hardware.gnss-service.spx.xml", 0o100644),
        (RC_TARGET, "android.hardware.gnss-service.spx.rc", 0o100644),
        (STAGE_TARGET, "spx-gnss-stage", 0o100755),
    ):
        bodies[target] = (GNSS_DIR / src_name).read_bytes().replace(b"\r\n", b"\n")
        records[target] = new_record(mode)

    return {
        "enabled": True,
        "aidl_version": 4,
        "hal_sha256": _sha256(hal_bytes),
    }
