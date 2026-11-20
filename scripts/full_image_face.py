"""Default Surface Pro X OV7251 IR IFace AIDL V4 HAL integration into initramfs."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile

from disk_image import require

ROOT = Path(__file__).resolve().parents[1]
FACE_DIR = ROOT / "probe/face"

HAL_TARGET = "etc/spx-face/android.hardware.biometrics.face-service.default"
CLI_TARGET = "etc/spx-face/spx-face-cli"
MANIFEST_TARGET = "etc/spx-face/android.hardware.biometrics.face-service.spx.xml"
PERM_TARGET = "etc/spx-face/android.hardware.biometrics.face.xml"
RC_TARGET = "etc/spx-face/android.hardware.biometrics.face-service.spx.rc"
STAGE_TARGET = "bin/spx-face-stage"

STAGE = b"qti-system-display-stage > /logs/qti-system-display.txt 2>&1 || fail qti-system-display\n"
STAGE_HOOK = b"spx-face-stage > /logs/face-stage.txt 2>&1 || echo SPX_FACE_STAGE_FAILED\n"


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


def install_targets() -> dict[str, str]:
    return {
        HAL_TARGET: "hal",
        CLI_TARGET: "cli",
        MANIFEST_TARGET: "android.hardware.biometrics.face-service.spx.xml",
        PERM_TARGET: "android.hardware.biometrics.face.xml",
        RC_TARGET: "android.hardware.biometrics.face-service.spx.rc",
        STAGE_TARGET: "spx-face-stage",
    }


def _hal_builder():
    spec = importlib.util.spec_from_file_location(
        "build_spx_face_hal", ROOT / "scripts/build-spx-face-hal.py"
    )
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def toolchains_available() -> bool:
    return _hal_builder().toolchains_available()


def build_default_payload() -> dict[str, bytes]:
    mod = _hal_builder()
    with tempfile.TemporaryDirectory(prefix="spx-face-hal-") as tmp:
        meta = mod.build_hal(Path(tmp))
        return {
            "hal": Path(meta["binary"]).read_bytes(),
            "cli": Path(meta["cli_binary"]).read_bytes(),
        }


def load(
    face_hal_path: Path | None = None,
    face_cli_path: Path | None = None,
    compiled_payload: dict[str, bytes] | None = None,
) -> dict[str, bytes] | None:
    if compiled_payload is not None:
        _verify_arm64_elf(compiled_payload["hal"], HAL_TARGET)
        if "cli" in compiled_payload and compiled_payload["cli"] is not None:
            _verify_arm64_elf(compiled_payload["cli"], CLI_TARGET)
        return dict(compiled_payload)
    if face_hal_path is not None:
        hal_bytes = Path(face_hal_path).read_bytes()
        _verify_arm64_elf(hal_bytes, str(face_hal_path))
        payload: dict[str, bytes] = {"hal": hal_bytes}
        cli_candidate = Path(face_cli_path) if face_cli_path else Path(face_hal_path).with_name("spx-face-cli")
        if cli_candidate.is_file():
            cli_bytes = cli_candidate.read_bytes()
            _verify_arm64_elf(cli_bytes, str(cli_candidate))
            payload["cli"] = cli_bytes
        return payload
    if toolchains_available():
        payload = build_default_payload()
        _verify_arm64_elf(payload["hal"], HAL_TARGET)
        _verify_arm64_elf(payload["cli"], CLI_TARGET)
        return payload
    return None


def install(records, bodies, payload: dict[str, bytes] | bytes, new_record) -> dict:
    if isinstance(payload, (bytes, bytearray)):
        hal_bytes = bytes(payload)
        cli_bytes = None
    else:
        hal_bytes = payload["hal"]
        cli_bytes = payload.get("cli")

    _verify_arm64_elf(hal_bytes, HAL_TARGET)
    if cli_bytes is not None:
        _verify_arm64_elf(cli_bytes, CLI_TARGET)

    init = bodies["init"]
    if b"spx-face-stage >" not in init:
        require(init.count(STAGE) == 1, "Face HAL requires QTI display stage anchor in init")
        init = init.replace(STAGE, STAGE + STAGE_HOOK)
    bodies["init"] = init

    bodies[HAL_TARGET] = hal_bytes
    records[HAL_TARGET] = new_record(0o100755)
    if cli_bytes is not None:
        bodies[CLI_TARGET] = cli_bytes
        records[CLI_TARGET] = new_record(0o100755)

    for target, src_name, mode in (
        (MANIFEST_TARGET, "android.hardware.biometrics.face-service.spx.xml", 0o100644),
        (PERM_TARGET, "android.hardware.biometrics.face.xml", 0o100644),
        (RC_TARGET, "android.hardware.biometrics.face-service.spx.rc", 0o100644),
        (STAGE_TARGET, "spx-face-stage", 0o100755),
    ):
        bodies[target] = (FACE_DIR / src_name).read_bytes().replace(b"\r\n", b"\n")
        records[target] = new_record(mode)

    return {
        "enabled": True,
        "aidl_version": 4,
        "security_class": "weak",
        "hal_sha256": _sha256(hal_bytes),
        "cli_sha256": _sha256(cli_bytes) if cli_bytes is not None else None,
    }
