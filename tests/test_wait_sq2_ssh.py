import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('wait_ssh', ROOT / 'scripts/wait-sq2-ssh.py')
wait = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wait)
OLD = 'f0652a54-ea7c-45e6-8265-a048c90fcdf0'
NEW = '5a54a9cc-0763-43b8-a3a1-6be48c44b01d'

class WaitSsh(unittest.TestCase):
    def run_wait(self, runner):
        now = [0.0]
        def sleep(seconds):
            now[0] += seconds
        events = []
        with patch.object(wait.time, 'monotonic', side_effect=lambda: now[0]), \
             patch.object(wait.time, 'sleep', side_effect=sleep), \
             patch.object(wait.subprocess, 'run', side_effect=runner):
            result = wait.wait_for_ssh(['ssh', 'host'], OLD, report=events.append)
        return result, events

    def test_refused_connection_waits_full_ten_minutes(self):
        result, events = self.run_wait(lambda *a, **kw: subprocess.CompletedProcess(a, 255, '', 'Connection refused'))
        self.assertFalse(result['ready'])
        self.assertEqual(result['elapsed_seconds'], 600)
        self.assertTrue(all(event['pending'] for event in events[:-1]))
        self.assertFalse(events[-1]['pending'])

    def test_old_boot_is_not_reboot_success(self):
        responses = iter([subprocess.CompletedProcess([], 0, OLD, ''),
                          subprocess.CompletedProcess([], 255, '', 'Connection refused'),
                          subprocess.CompletedProcess([], 0, NEW, '')])
        result, events = self.run_wait(lambda *a, **kw: next(responses))
        self.assertTrue(result['ready'])
        self.assertEqual(result['boot_id'], NEW)
        self.assertEqual(result['elapsed_seconds'], 20)
        self.assertEqual(events[0]['error'], 'old boot still running')

    def test_timeout_and_invalid_stdout_do_not_count_as_ready(self):
        calls = [0]
        def runner(*args, **kwargs):
            calls[0] += 1
            self.assertLessEqual(kwargs['timeout'], 15)
            if calls[0] == 1:
                raise subprocess.TimeoutExpired(args, 15)
            if calls[0] == 2:
                return subprocess.CompletedProcess([], 0, 'not-a-boot-id', '')
            return subprocess.CompletedProcess([], 0, NEW, '')
        result, _ = self.run_wait(runner)
        self.assertEqual(result['attempts'], 3)
        self.assertTrue(result['ready'])

if __name__ == '__main__':
    unittest.main()
