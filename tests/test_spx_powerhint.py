"""Validation tests for configs/powerhint.sq2.json and full_image power/EAS wiring."""
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import full_image as fi_mod


class SpxPowerhintTests(unittest.TestCase):
    def test_powerhint_schema_values_and_priority_order(self):
        path = ROOT / "configs/powerhint.sq2.json"
        raw = path.read_bytes()
        self.assertNotIn(b"\r\n", raw)
        cfg = json.loads(raw.decode("utf-8"))

        nodes = {n["Name"]: n for n in cfg["Nodes"]}
        self.assertEqual(len(nodes), len(cfg["Nodes"]), "Duplicate Node names")
        for req_node in (
            "TopAppSchedulerWeight",
            "TopAppUclampMin",
            "TopAppUclampMax",
            "ForegroundUclampMax",
            "BackgroundUclampMax",
            "SystemBackgroundUclampMax",
            "Dex2oatUclampMax",
        ):
            self.assertIn(req_node, nodes)

        for name, node in nodes.items():
            self.assertTrue(node["Path"].startswith("/dev/cpuctl/"), f"Unexpected path on {name}")
            self.assertNotIn("latency_sensitive", node["Path"], "Mainline 6.18 lacks latency_sensitive")
            self.assertTrue(node.get("ResetOnInit"), f"{name} must ResetOnInit")
            vals = node["Values"]
            idx = node["DefaultIndex"]
            self.assertIsInstance(vals, list)
            self.assertTrue(0 <= idx < len(vals))

        # Verify libperfmgr priority ordering (index 0 = highest priority).
        self.assertEqual(nodes["TopAppUclampMin"]["Values"][-1], "0")
        self.assertEqual(nodes["TopAppUclampMin"]["DefaultIndex"], len(nodes["TopAppUclampMin"]["Values"]) - 1)
        self.assertEqual(nodes["BackgroundUclampMax"]["Values"][nodes["BackgroundUclampMax"]["DefaultIndex"]], "35")
        self.assertEqual(nodes["BackgroundUclampMax"]["Values"][0], "20")

        seen_hints = set()
        for act in cfg["Actions"]:
            self.assertIn(act["Node"], nodes)
            self.assertIn(act["Value"], nodes[act["Node"]]["Values"])
            self.assertIsInstance(act["Duration"], int)
            self.assertGreaterEqual(act["Duration"], 0)
            seen_hints.add(act["PowerHint"])

        for req_hint in (
            "INTERACTION",
            "LAUNCH",
            "LOW_POWER",
            "PROFILE_VIDEO_PLAYBACK",
            "PROFILE_AC_POLICY",
            "DISPLAY_INACTIVE",
            "DEVICE_IDLE",
        ):
            self.assertIn(req_hint, seen_hints)

    def test_full_image_installs_powerhint_and_eas_dtb(self):
        records, bodies = {}, {}
        fi_mod.install_powerhint(records, bodies)
        key = "etc/googlebook-security/etc/powerhint.json"
        self.assertIn(key, bodies)
        self.assertEqual(
            json.loads(bodies[key].decode("utf-8")),
            json.loads((ROOT / "configs/powerhint.sq2.json").read_text("utf-8")),
        )
        fi_src = (ROOT / "scripts/full_image.py").read_text("utf-8")
        self.assertIn("prepare-spx-cpu-eas-dtb.py", fi_src)
        self.assertIn("install_powerhint(records, bodies)", fi_src)
        self.assertIn("install_event_watchers(records, bodies", fi_src)


if __name__ == "__main__":
    unittest.main()
