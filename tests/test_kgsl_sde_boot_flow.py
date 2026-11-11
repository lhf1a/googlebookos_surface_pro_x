import copy
import importlib.util
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("bootflow", ROOT / "scripts/check-kgsl-sde-boot-flow.py")
FLOW = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FLOW)


class BootFlowGraphTests(unittest.TestCase):
    def setUp(self):
        self.flow = json.loads((ROOT / "configs/kgsl-sde-boot-flow.json").read_text())

    def test_current_graph_is_incomplete_not_device_approval(self):
        result = FLOW.check(self.flow)
        self.assertTrue(result["graph_valid"])
        self.assertFalse(result["host_construction_complete"])
        self.assertFalse(result["device_activation_allowed"])

    def test_deleting_labels_does_not_create_approval(self):
        self.flow["host_blockers"] = {}
        self.assertFalse(FLOW.check(self.flow)["device_activation_allowed"])

    def test_stage_before_host_validation_rejected(self):
        self.flow["states"][0], self.flow["states"][4] = self.flow["states"][4], self.flow["states"][0]
        with self.assertRaises(ValueError):
            FLOW.check(self.flow)

    def test_cycle_rejected(self):
        self.flow["states"][2]["next"] = "VERIFY_INPUTS"
        with self.assertRaises(ValueError):
            FLOW.check(self.flow)

    def test_missing_gate_rejected(self):
        self.flow["states"][2]["requires"] = []
        with self.assertRaises(ValueError):
            FLOW.check(self.flow)

    def test_retry_rejected(self):
        self.flow["runtime_failure"][-1] = "retry"
        with self.assertRaises(ValueError):
            FLOW.check(self.flow)

    def test_modprobe_as_bind_rejected(self):
        self.flow["runtime_binding_checks"]["modprobe_zero_is_not_binding_success"] = False
        with self.assertRaises(ValueError):
            FLOW.check(self.flow)

    def test_unknown_blocker_rejected(self):
        self.flow["host_blockers"]["UNKNOWN"] = "not a stage gate"
        with self.assertRaises(ValueError):
            FLOW.check(self.flow)


if __name__ == "__main__":
    unittest.main()
