"""Host-only tests for the adapted A6xx upstream batch on the 6.18.60 source pin.

The first test classes check the real artifacts (11 mail-style patch files, the
20261111 series config, and the Lead-verified metadata/hash inputs when they are
in the workspace). The last class exercises the prepare script's validation and
check/apply path against a synthetic archive and mini series in a temporary
directory. Nothing here builds a kernel, touches a device or claims hardware
validation.
"""
import difflib
import hashlib
import importlib.util
import json
import sys
from pathlib import Path
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
CONFIG = ROOT / "configs/drm-618-a6xx-upstream-20261111.json"
PATCH_DIR = ROOT / "patches/drm-618-a6xx-upstream"

spec = importlib.util.spec_from_file_location(
    "a6xx_prepare", ROOT / "scripts/prepare-drm-618-a6xx-upstream.py")
prepare = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = prepare
spec.loader.exec_module(prepare)

REPO = prepare.ROOT
LEAD_INPUT = REPO / "out/a6xx-work"
ROLES = {"prerequisite", "fix"}
ORIGINS = {"backport-subset", "pending-upstream", "merged-mainline"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def digest_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class SeriesArtifacts(unittest.TestCase):
    """The real 11-patch batch must stay internally consistent and clearly bounded."""

    @classmethod
    def setUpClass(cls):
        cls.config, cls.config_path = prepare.load_config(CONFIG)
        cls.config_root = cls.config_path.parents[1]

    def test_schema_pins_and_boundaries(self):
        config = self.config
        self.assertEqual(config["schema"], 1)
        self.assertEqual(config["kernel_version"], "6.18.60")
        self.assertFalse(config["hardware_validated"])
        self.assertEqual(config["source"]["source_subdir"], "kernel")
        self.assertRegex(config["source"]["archive_sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(config["apply"]["tool"], "patch")
        self.assertTrue(config["limits"])
        self.assertTrue(config["rollback"])
        self.assertEqual(config["upstream"]["status_checked"], "2026-11-11")

    def test_batch_shape_and_order(self):
        patches = self.config["patches"]
        self.assertEqual(len(patches), 11)
        self.assertEqual([entry["index"] for entry in patches], list(range(1, 12)))
        names = [Path(entry["path"]).name for entry in patches]
        self.assertEqual(names, sorted(names))
        self.assertEqual([name[:4] for name in names],
                         ["%.4d" % index for index in range(1, 12)])
        self.assertEqual(len(set(entry["path"] for entry in patches)), 11)
        roles = [entry["role"] for entry in patches]
        self.assertEqual(roles[0], "prerequisite")
        self.assertTrue(all(role == "fix" for role in roles[1:]))
        self.assertTrue(all(entry["role"] in ROLES for entry in patches))
        self.assertTrue(all(entry["origin"] in ORIGINS for entry in patches))

    def test_batch_composition_matches_origins(self):
        composition = self.config["upstream"]["batch_composition"]
        self.assertEqual(composition["prerequisite_subset"], [1])
        self.assertEqual(composition["pending_a680_fixes"], [2, 3, 4, 5])
        self.assertEqual(composition["merged_mainline_backports"], [6, 7, 8, 9, 10, 11])
        by_index = {entry["index"]: entry for entry in self.config["patches"]}
        self.assertEqual(by_index[1]["origin"], "backport-subset")
        for index in (2, 3, 4, 5):
            self.assertEqual(by_index[index]["origin"], "pending-upstream")
            self.assertNotIn("upstream_commit", by_index[index])
        for index in (6, 7, 8, 9, 10, 11):
            self.assertEqual(by_index[index]["origin"], "merged-mainline")
            self.assertRegex(by_index[index]["upstream_commit"], r"^[0-9a-f]{40}$")

    def test_patch_files_match_the_configured_sha256(self):
        entries = prepare.verify_patch_files(self.config, self.config_root)
        self.assertEqual(len(entries), 11)
        for entry, configured in zip(entries, self.config["patches"]):
            path = self.config_root / configured["path"]
            self.assertTrue(path.is_file(), configured["path"])
            self.assertTrue(str(path).startswith(str(self.config_root / "patches") + "/"))
            self.assertEqual(entry["sha256"], configured["sha256"])
            self.assertEqual(entry["role"], configured["role"])

    def test_patch_files_match_lead_sha256sums(self):
        sums_file = LEAD_INPUT / "patches/SHA256SUMS"
        if not sums_file.is_file():
            self.skipTest("Lead patch input (out/a6xx-work) is not present")
        sums = {}
        for line in sums_file.read_text(encoding="utf-8").splitlines():
            value, name = line.split()
            sums[name] = value
        self.assertEqual(len(sums), 11)
        for configured in self.config["patches"]:
            name = Path(configured["path"]).name
            self.assertIn(name, sums)
            self.assertEqual(configured["sha256"], sums[name])
            self.assertEqual(digest_file(self.config_root / configured["path"]), sums[name])

    def test_config_matches_lead_patch_hashes(self):
        hashes_file = LEAD_INPUT / "patch-hashes.json"
        if not hashes_file.is_file():
            self.skipTest("Lead patch input (out/a6xx-work) is not present")
        hashes = json.loads(hashes_file.read_text(encoding="utf-8"))
        self.assertEqual(len(hashes["patches"]), 11)
        for entry in hashes["patches"]:
            configured = self.config["patches"][entry["index"] - 1]
            self.assertEqual(configured["sha256"], entry["sha256"])
            expected = {"kernel/" + name: values for name, values in entry["files"].items()}
            self.assertEqual(configured["files"], expected)

    def test_config_matches_lead_patch_meta(self):
        meta_file = LEAD_INPUT / "patch-meta.json"
        if not meta_file.is_file():
            self.skipTest("Lead patch input (out/a6xx-work) is not present")
        meta = {entry["index"]: entry for entry in
                json.loads(meta_file.read_text(encoding="utf-8"))}
        self.assertEqual(len(meta), 11)
        for configured in self.config["patches"]:
            expected = meta[configured["index"]]
            for key in ("path", "sha256", "subject", "role", "origin", "message_id"):
                self.assertEqual(configured[key], expected[key], key)
            for key in ("author", "date", "upstream_commit", "patchwork", "url", "series"):
                if key in expected:
                    self.assertEqual(configured.get(key), expected[key], key)

    def test_metadata_is_complete_for_every_patch(self):
        message_ids = set()
        for entry in self.config["patches"]:
            for key in ("subject", "author", "date", "message_id", "url"):
                self.assertTrue(entry.get(key), "%s missing %s" % (entry["path"], key))
            self.assertRegex(entry["message_id"], r"^<[^>]+>$")
            self.assertNotIn(entry["message_id"], message_ids)
            message_ids.add(entry["message_id"])
            self.assertTrue(entry["local_adaptation"])
            self.assertTrue(entry["post_contains"])
            self.assertTrue(entry["files"])
        self.assertEqual(len(message_ids), 11)

    def test_per_file_before_after_chain_is_consistent(self):
        touched = prepare.verify_series_config(self.config)
        self.assertEqual(len(touched), 7)
        for name, hashes in touched.items():
            self.assertRegex(hashes, r"^[0-9a-f]{64}$")
            self.assertTrue(name.startswith("kernel/drivers/gpu/drm/msm/"))
        self.assertIn("kernel/drivers/gpu/drm/msm/msm_gpu.c", touched)

    def test_checks_cover_every_touched_file(self):
        touched = {name for entry in self.config["patches"] for name in entry["files"]}
        for section in ("baseline", "preconditions", "markers", "final_absent"):
            self.assertTrue(self.config[section], section)
            for item in self.config[section]:
                self.assertIn(item["file"], touched, section)
        blob = json.dumps(self.config["markers"])
        for required in ("a680_hwcg", "gmem_range_min = 0;", "MSM_PARAM_GMEM_BASE",
                         "GMU_GX_GMU_SP_RF_CONTROL_0", "gbif_cx", "dev_pm_opp_put",
                         "100, 100000);", "pri[pindex]", "disable_err_irq = false;"):
            self.assertIn(required, blob)
        baseline = json.dumps(self.config["baseline"])
        for required in ("a680_sqe.fw", "100, 10000", "sec[j] >= level", "disable_err_irq"):
            self.assertIn(required, baseline)

    def test_patch_mail_provenance_is_intact(self):
        for configured in self.config["patches"]:
            text = (self.config_root / configured["path"]).read_text(encoding="utf-8")
            for header in ("From: ", "Subject: ", "Date: ", "Signed-off-by: "):
                self.assertIn(header, text)
            self.assertIn("diff --git ", text)
            self.assertIn(configured["subject"], text)
            self.assertIn("6.18.60", text)
            self.assertTrue("adaptation" in text.lower() or "applied unchanged" in text.lower(),
                            configured["path"] + " lost its 6.18.60 adaptation note")
            self.assertTrue("Message-ID: " in text or "https://patchwork.freedesktop.org/" in text
                            or "https://patchew.org/" in text,
                            configured["path"] + " lost its upstream posting provenance")

    def test_baseline_kernel_config_pin_matches(self):
        pinned = self.config["kernel_build_config"]
        path = REPO / pinned["path"]
        if not path.is_file():
            self.skipTest("baseline kernel config is not present")
        self.assertEqual(digest_file(path), pinned["sha256"])
        self.assertIn(pinned["localversion"], path.read_text(encoding="utf-8"))

    def test_prerequisite_analysis_is_recorded(self):
        prerequisite = self.config["prerequisite_analysis"]
        self.assertFalse(prerequisite["present_in_6.18.60"])
        self.assertIn("drop 0001", prerequisite["risk"])
        upstream = self.config["upstream"]
        self.assertEqual(upstream["primary_series"]["status"], "pending-upstream")
        self.assertIn("not a single upstream series", upstream["status_note"])


class PinnedArchive(unittest.TestCase):
    """Integration checks that only run when the private pinned archive is present."""

    @classmethod
    def setUpClass(cls):
        cls.config, _ = prepare.load_config(CONFIG)
        cls.archive = REPO / cls.config["source"]["archive"]

    def setUp(self):
        if not self.archive.is_file():
            self.skipTest("pinned 6.18.60 source archive is not present")

    def test_archive_sha256_and_size(self):
        source = self.config["source"]
        self.assertEqual(prepare.verify_archive(self.archive, source["archive_sha256"],
                                                source["archive_bytes"]),
                         source["archive_sha256"])

    def test_check_rehearses_the_batch_on_the_pinned_archive(self):
        report = prepare.prepare(CONFIG, mode="check")
        self.assertEqual(report["mode"], "check")
        self.assertTrue(report["temporary_tree"])
        self.assertEqual(len(report["patches"]), 11)
        self.assertEqual(len(report["markers"]), len(self.config["markers"]))
        self.assertEqual(len(report["baseline"]), len(self.config["baseline"]))
        self.assertEqual(len(report["final_absent"]), len(self.config["final_absent"]))
        self.assertEqual(report["kernel_version"], "6.18.60")
        self.assertEqual(report["apply_tool"], ["patch"])
        self.assertFalse(report["compiled"])
        self.assertFalse(report["hardware_validated"])


class PrepareFixture(unittest.TestCase):
    """Mechanism tests against a synthetic 6.18.60 archive and mini series."""

    HEADER = "kernel/drivers/fixture.h"
    SOURCE = "kernel/drivers/fixture.c"

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "configs").mkdir()
        (self.root / "patches").mkdir()
        self.tree = self.root / "tree"
        self.base = {
            self.HEADER: "/* fixture */\nHEADER\n",
            self.SOURCE: "/* fixture */\nBASE\n",
            "kernel/Makefile": "VERSION = 6\nPATCHLEVEL = 18\nSUBLEVEL = 60\n",
        }
        self.write_tree(self.base)
        self.states = {
            "prerequisite": {
                self.HEADER: self.base[self.HEADER] + "gbif_cx\n",
                self.SOURCE: self.base[self.SOURCE] + "GBIF\n",
            },
            "fix": {
                self.SOURCE: self.base[self.SOURCE] + "GBIF\nHWCG\n",
            },
        }
        self.patch1 = self.make_patch("0001-fixture-prerequisite.patch",
                                      [self.HEADER, self.SOURCE], self.base,
                                      self.states["prerequisite"],
                                      "[PATCH 1/2] fixture: add gbif_cx")
        self.patch2 = self.make_patch("0002-fixture-fix.patch", [self.SOURCE],
                                      self.states["prerequisite"], self.states["fix"],
                                      "[PATCH 2/2] fixture: add HWCG")
        self.archive = self.root / "fixture.tar.gz"
        self.write_archive(self.base)
        self.config_path = self.root / "configs/fixture.json"
        self.write_config()
        self.work = self.root / "work"

    def write_tree(self, files):
        for name, text in files.items():
            path = self.tree / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8")

    def write_archive(self, files):
        self.write_tree(files)
        with tarfile.open(self.archive, "w:gz") as tar:
            tar.add(self.tree / "kernel", arcname="kernel")
        return digest_file(self.archive)

    def make_patch(self, name, touched, before, after, subject):
        chunks = []
        for target in touched:
            old = before[target].splitlines(keepends=True)
            new = after[target].splitlines(keepends=True)
            diff = "".join(difflib.unified_diff(
                old, new, fromfile="a/" + target.removeprefix("kernel/"),
                tofile="b/" + target.removeprefix("kernel/")))
            chunks.append(diff)
        path = self.root / "patches" / name
        path.write_text(
            "From: Fixture Author <fixture@example.invalid>\n"
            "Date: Mon, 2 Nov 2026 22:13:50 -0300\n"
            "Subject: " + subject + "\n"
            "\n"
            "Synthetic fixture commit message.\n"
            "\n"
            "Signed-off-by: Fixture Author <fixture@example.invalid>\n"
            "\n---\n" + "".join(chunks), encoding="utf-8")
        return path

    def write_config(self, mutate=None):
        before, prerequisite, fix = self.base, self.states["prerequisite"], self.states["fix"]
        config = {
            "schema": 1,
            "name": "fixture",
            "status": "fixture-host-only",
            "kernel_version": "6.18.60",
            "hardware_validated": False,
            "source": {
                "archive": str(self.archive),
                "archive_sha256": digest_file(self.archive),
                "archive_bytes": self.archive.stat().st_size,
                "source_subdir": "kernel",
            },
            "patches": [
                {"path": "patches/" + self.patch1.name, "sha256": digest_file(self.patch1),
                 "role": "prerequisite", "origin": "pending-upstream",
                 "files": {name: {"before": digest(before[name].encode()),
                                  "after": digest(prerequisite[name].encode())}
                           for name in (self.HEADER, self.SOURCE)},
                 "post_contains": [{"file": self.HEADER, "contains": "gbif_cx"}]},
                {"path": "patches/" + self.patch2.name, "sha256": digest_file(self.patch2),
                 "role": "fix", "origin": "merged-mainline",
                 "files": {self.SOURCE: {
                     "before": digest(prerequisite[self.SOURCE].encode()),
                     "after": digest(fix[self.SOURCE].encode())}},
                 "post_contains": [{"file": self.SOURCE, "contains": "HWCG"}]},
            ],
            "baseline": [
                {"id": "base-header", "file": self.HEADER, "contains": "HEADER"},
            ],
            "preconditions": [
                {"id": "pre-gbif", "file": self.HEADER, "absent": "gbif_cx"},
                {"id": "pre-hwcg", "file": self.SOURCE, "absent": "HWCG"},
            ],
            "markers": [
                {"id": "m-gbif", "file": self.HEADER, "contains": "gbif_cx"},
                {"id": "m-hwcg", "file": self.SOURCE, "contains": "HWCG"},
            ],
            "final_absent": [
                {"id": "no-old-base", "file": self.SOURCE, "absent": "OLDSTATE"},
            ],
            "limits": ["fixture only"],
        }
        if mutate:
            mutate(config)
        self.config = config
        self.config_path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")
        return config

    def update_archive_pin(self):
        self.write_config(lambda config: config["source"].update(
            archive_sha256=digest_file(self.archive),
            archive_bytes=self.archive.stat().st_size))

    def test_check_rehearses_without_writing_the_work_tree(self):
        report = prepare.prepare(self.config_path, mode="check")
        self.assertEqual(report["mode"], "check")
        self.assertEqual(len(report["patches"]), 2)
        self.assertEqual(report["apply_tool"], ["patch"])
        self.assertEqual(report["kernel_version"], "6.18.60")
        self.assertEqual(report["baseline"], ["base-header"])
        self.assertEqual(report["final_absent"], ["no-old-base"])
        self.assertFalse(self.work.exists())

    def test_apply_creates_verified_tree_and_manifest(self):
        report = prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertEqual(report["mode"], "apply")
        self.assertEqual(report["status"], "fixture-host-only")
        self.assertTrue((self.work / self.SOURCE).read_text().endswith("GBIF\nHWCG\n"))
        self.assertIn("gbif_cx", (self.work / self.HEADER).read_text())
        manifest = json.loads((self.work / "SOURCE.json").read_text())
        self.assertEqual(manifest["mode"], "apply")
        self.assertEqual(manifest["markers"], ["m-gbif", "m-hwcg"])
        self.assertEqual(manifest["final_absent"], ["no-old-base"])
        self.assertFalse(manifest["compiled"])
        self.assertFalse(manifest["hardware_validated"])
        self.assertEqual(manifest["apply_tool"], ["patch"])
        self.assertEqual(digest_file(self.root / "tree" / self.SOURCE),
                         digest(self.base[self.SOURCE].encode()))

    def test_apply_refuses_a_reused_work_directory(self):
        prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        with self.assertRaisesRegex(ValueError, "fresh"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")

    def test_tampered_patch_is_rejected_before_any_work(self):
        self.patch2.write_text(self.patch2.read_text() + "tampered\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Patch 2 sha256 mismatch"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertFalse(self.work.exists())

    def test_unexpected_base_file_fails_atomically(self):
        changed = dict(self.base, **{self.SOURCE: self.base[self.SOURCE] + "EXTRA\n"})
        self.write_archive(changed)
        self.update_archive_pin()
        with self.assertRaisesRegex(ValueError, "before-patch hash"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertFalse(self.work.exists())

    def test_missing_baseline_marker_is_rejected(self):
        self.write_config(lambda config: config["baseline"].append(
            {"id": "baseline-absent", "file": self.SOURCE, "contains": "NOT-IN-PRISTINE"}))
        with self.assertRaisesRegex(ValueError, "Baseline marker"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertFalse(self.work.exists())

    def test_missing_marker_fails_and_removes_the_work_tree(self):
        def mutate(config):
            config["markers"] = [{"id": "absent", "file": self.SOURCE, "contains": "ABSENT"}]
        self.write_config(mutate)
        with self.assertRaisesRegex(ValueError, "Final marker"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertFalse(self.work.exists())

    def test_forbidden_final_pattern_fails_and_removes_the_work_tree(self):
        self.write_config(lambda config: config["final_absent"].append(
            {"id": "forbidden", "file": self.SOURCE, "absent": "HWCG"}))
        with self.assertRaisesRegex(ValueError, "Final forbidden"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertFalse(self.work.exists())

    def test_present_precondition_is_rejected(self):
        polluted = dict(self.base, **{self.HEADER: self.base[self.HEADER] + "gbif_cx\n"})
        self.write_archive(polluted)
        self.update_archive_pin()
        with self.assertRaisesRegex(ValueError, "already contains"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertFalse(self.work.exists())

    def test_wrong_kernel_version_is_rejected(self):
        wrong = dict(self.base, **{"kernel/Makefile": "VERSION = 6\nPATCHLEVEL = 19\nSUBLEVEL = 0\n"})
        self.write_archive(wrong)
        self.update_archive_pin()
        with self.assertRaisesRegex(ValueError, "Unsupported kernel source 6.19.0"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="apply")
        self.assertFalse(self.work.exists())

    def test_out_of_order_series_is_rejected_by_the_hash_chain(self):
        self.write_config(lambda config: config["patches"].reverse())
        with self.assertRaisesRegex(ValueError, "before hash does not follow"):
            prepare.prepare(self.config_path, work_dir=self.work, mode="check")

    def test_cli_check_and_failure_exit_codes(self):
        self.assertEqual(prepare.main(["--check", "--config", str(self.config_path)]), 0)
        self.patch1.write_text("broken\n", encoding="utf-8")
        self.assertEqual(prepare.main(["--check", "--config", str(self.config_path)]), 1)
        with self.assertRaises(SystemExit):
            prepare.main(["--apply", "--config", str(self.config_path)])


if __name__ == "__main__":
    unittest.main()
