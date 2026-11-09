#!/usr/bin/env python3
"""Verify capture queue deferral using the actual paired Venus helper functions."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import resource
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('omx_patch', ROOT/'scripts/patch-sq2-venus-omx.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)


def functions(source):
    result = []
    for signature in ('void venus_helper_vb2_buf_queue(',
                      'int venus_helper_process_initial_cap_bufs('):
        if source.count(signature) != 1:
            raise ValueError('Unknown Venus helper function: ' + signature)
        start = source.index(signature)
        end = source.index('\n}', start) + 2
        result.append(source[start:end])
    return '\n\n'.join(result)


def run_source(directory, output):
    original = (directory/'helpers.c').read_text()
    output.mkdir(parents=True, exist_ok=False)
    candidate = output/'candidate'
    candidate.mkdir()
    (candidate/'helpers.c').write_text(original)
    patch.apply_capture_start(candidate)
    fixed = (candidate/'helpers.c').read_text()
    template = (ROOT/'tests/fixtures/venus_sq2_capture_start.c.in').read_text()
    results = {}
    def no_core():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    for name, source in [('old', original), ('fixed', fixed)]:
        cfile, binary = output/(name+'.c'), output/name
        cfile.write_text(template.replace('@FUNCTIONS@', functions(source)))
        command = ['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                   '-fno-pie', '-no-pie', str(cfile), '-o', str(binary)]
        compiled = subprocess.run(command, text=True, capture_output=True)
        (output/(name+'-build.txt')).write_text(compiled.stdout + compiled.stderr)
        compiled.check_returncode()
        result = subprocess.run([str(binary)], text=True, capture_output=True,
                                timeout=10, preexec_fn=no_core)
        (output/(name+'-run.txt')).write_text(result.stdout + result.stderr)
        results[name] = {'command': command, 'exit': result.returncode,
                         'source_sha256': hashlib.sha256(source.encode()).hexdigest()}
    (output/'RESULT.json').write_text(json.dumps(results, indent=2)+'\n')
    if results['old']['exit'] == 0:
        raise AssertionError('Old source did not reproduce premature capture submission')
    old_error = (output/'old-run.txt').read_text()
    if 'process_calls==0 && !cache_calls && !ref_checks && !error_calls' not in old_error:
        raise AssertionError('Old source failed outside the expected regression assertion')
    if results['fixed']['exit'] != 0:
        raise AssertionError('Fixed actual source failed ownership/state cases')
    print('CAPTURE_START_ACTUAL_SOURCE_PASS old_regression_detected=1 cases=14')


class CaptureStartAdapter(unittest.TestCase):
    def test_unknown_source_is_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            p = d/'helpers.c'
            p.write_text('void venus_helper_vb2_buf_queue(void *vb)\n{\n unknown(vb);\n}\n')
            before = p.read_bytes()
            with self.assertRaises(ValueError):
                patch.apply_capture_start(d)
            self.assertEqual(p.read_bytes(), before)

    def test_only_queue_callback_changes_and_repeat_refused(self):
        before = ('void venus_helper_vb2_buf_queue(void *vb)\n{\n'
                  '\tv4l2_m2m_buf_queue(m2m_ctx, vbuf);\n'
                  '\tif (inst->codec_state == VENUS_DEC_STATE_DRC)\n\t\treturn;\n'
                  '\tcache_payload(inst, vb);\n}\n'
                  'int venus_helper_process_initial_cap_bufs(void *inst)\n{\n'
                  '\treturn existing_start(inst);\n}\n')
        with tempfile.TemporaryDirectory() as tmp:
            d = Path(tmp)
            p = d/'helpers.c'
            p.write_text(before)
            patch.apply_capture_start(d)
            after = p.read_text()
            self.assertEqual(after.split('int venus_helper_process_initial_cap_bufs')[1],
                             before.split('int venus_helper_process_initial_cap_bufs')[1])
            self.assertLess(after.index('v4l2_m2m_buf_queue'), after.index('!inst->streamon_cap'))
            self.assertLess(after.index('!inst->streamon_cap'), after.index('cache_payload'))
            with self.assertRaises(ValueError):
                patch.apply_capture_start(d)
            self.assertEqual(p.read_text(), after)


if __name__ == '__main__':
    if '--source' in sys.argv:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--source', type=Path, required=True)
        parser.add_argument('--output', type=Path, required=True)
        args = parser.parse_args()
        run_source(args.source, args.output)
    else:
        unittest.main()
