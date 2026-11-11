#!/usr/bin/env python3
"""Execute actual SDE PHY parsing, support, catalog and controller functions.

AUX/MMIO and callback leaves are explicit models. Native mode uses only current
DRM definitions; --donor-reference supplies pinned old wire definitions solely
for a semantic reference, never to a kernel build or candidate product.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def extract(text, name):
    matches = list(re.finditer(r"^(?:static\s+)?(?:inline\s+)?(?:int|bool|void|char\s*\*)\s*" + name + r"\([^;{]*\)\n\{.*?^\}", text, re.M | re.S))
    if len(matches) != 1:
        raise ValueError("expected exactly one actual function: " + name)
    return matches[0].group()


def macro(text, name):
    return re.search(r"^#\s*define\s+" + name + r"(?:\([^\n)]*\))?\s+[^\n]+", text, re.M).group()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', required=True, type=Path, help='prepared techpack/display/msm')
    p.add_argument('--kernel-source', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--donor-reference', type=Path, help='pinned donor drm_dp_helper.h, semantic-only')
    p.add_argument('--check-wiring', action='store_true')
    a = p.parse_args()
    a.output = a.output.absolute()
    a.output.mkdir(parents=True, exist_ok=False)
    paths = {n: a.source / ('dp/' + n) for n in ['dp_link.h', 'dp_link.c', 'dp_catalog.c', 'dp_ctrl.c', 'dp_reg.h']}
    text = {n: q.read_text() for n, q in paths.items()}
    core = a.kernel_source / 'include/drm/display/drm_dp.h'
    files = list(paths.values()) + [core, Path(__file__), ROOT / 'tests/sde_dp_phy_test.c']
    definitions = []
    for name in ['DP_PHY_TEST_PATTERN', 'DP_PHY_TEST_PATTERN_NONE', 'DP_PHY_TEST_PATTERN_D10_2',
                 'DP_PHY_TEST_PATTERN_ERROR_COUNT', 'DP_PHY_TEST_PATTERN_PRBS7',
                 'DP_PHY_TEST_PATTERN_80BIT_CUSTOM', 'DP_PHY_TEST_PATTERN_CP2520',
                 'DP_LINK_QUAL_PATTERN_CP2520_PAT_2', 'DP_LINK_QUAL_PATTERN_CP2520_PAT_3']:
        definitions.append(macro(core.read_text(), name))
    if a.donor_reference:
        if sha(a.donor_reference) != 'f739339626682a580b1e874695715c76bcf7e2534964ca4eee0bde01bb16f664':
            raise ValueError('not the pinned donor header')
        files.append(a.donor_reference)
        donor = a.donor_reference.read_text()
        for name in ['DP_TEST_PHY_PATTERN', 'DP_TEST_PHY_PATTERN_NONE', 'DP_TEST_PHY_PATTERN_D10_2_NO_SCRAMBLING',
                     'DP_TEST_PHY_PATTERN_SYMBOL_ERR_MEASUREMENT_CNT', 'DP_TEST_PHY_PATTERN_PRBS7',
                     'DP_TEST_PHY_PATTERN_80_BIT_CUSTOM_PATTERN', 'DP_TEST_PHY_PATTERN_CP2520_PATTERN_1',
                     'DP_TEST_PHY_PATTERN_CP2520_PATTERN_2', 'DP_TEST_PHY_PATTERN_CP2520_PATTERN_3']:
            definitions.append(macro(donor, name))
        definitions.append('#define DONOR_REFERENCE 1')
    enum = re.search(r'enum sde_dp_phy_test_pattern \{.*?\n};', text['dp_link.h'], re.S)
    if enum:
        definitions.append(enum.group())
    definitions.append(macro(text['dp_link.h'], 'DP_LINK_ENUM_STR'))
    for name in ['DP_MAINLINK_CTRL', 'DP_STATE_CTRL', 'DP_MAINLINK_LEVELS',
                 'DP_HBR2_COMPLIANCE_SCRAMBLER_RESET', 'DP_TEST_80BIT_CUSTOM_PATTERN_REG0',
                 'DP_TEST_80BIT_CUSTOM_PATTERN_REG1', 'DP_TEST_80BIT_CUSTOM_PATTERN_REG2']:
        definitions.append(macro(text['dp_reg.h'], name))
    for name in ['MR_LINK_TRAINING1', 'MR_LINK_SYMBOL_ERM', 'MR_LINK_PRBS7', 'MR_LINK_CUSTOM80', 'MR_LINK_TRAINING4']:
        definitions.append(macro(text['dp_ctrl.c'], name))
    functions = [extract(text[n], name) for n, name in [
        ('dp_link.h', 'dp_link_get_phy_test_pattern'), ('dp_link.c', 'dp_link_is_phy_test_pattern_supported'),
        ('dp_link.c', 'dp_link_parse_phy_test_params'), ('dp_catalog.c', 'dp_catalog_ctrl_send_phy_pattern'),
        ('dp_ctrl.c', 'dp_ctrl_send_phy_test_pattern')]]
    wiring = {}
    if a.check_wiring:
        alltext = '\n'.join(text.values())
        wiring = dict(no_old_pattern_symbols=not re.search(r'\bDP_TEST_PHY_PATTERN\w*', alltext),
                      no_mask_used_as_pattern='DP_PHY_TEST_PATTERN_SEL_MASK' not in alltext,
                      explicit_cp2_cp3=enum is not None and 'DP_LINK_QUAL_PATTERN_CP2520_PAT_2' in enum.group() and 'DP_LINK_QUAL_PATTERN_CP2520_PAT_3' in enum.group(),
                      modern_dpcd_register='DP_PHY_TEST_PATTERN,' in functions[2],
                      actual_caller_sends_requested='ctrl->catalog->send_phy_pattern(ctrl->catalog, pattern_requested);' in functions[4])
        if not all(wiring.values()):
            raise ValueError('wiring failure: ' + repr(wiring))
    (a.output / 'actual-definitions.h').write_text('\n'.join(definitions) + '\n')
    (a.output / 'actual-functions.h').write_text('\n\n'.join(functions) + '\n')
    runs = []
    for label, command in [
        ('compile', ['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                     '-fno-pie', '-no-pie', '-I', str(a.output), str(ROOT / 'tests/sde_dp_phy_test.c'), '-o', str(a.output / 'check')]),
        ('run', [str(a.output / 'check'), str(a.output / 'semantic.txt')])]:
        r = subprocess.run(command, capture_output=True, text=True, timeout=30, env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
        runs.append(dict(label=label, command=command, stdout=r.stdout, stderr=r.stderr, exit_status=r.returncode))
        (a.output / (label + '.stdout.txt')).write_text(r.stdout)
        (a.output / (label + '.stderr.txt')).write_text(r.stderr)
        if r.returncode:
            break
    result = dict(status='pass' if runs[-1]['exit_status'] == 0 else 'fail', donor_reference=bool(a.donor_reference),
                  functions=['dp_link_get_phy_test_pattern', 'dp_link_is_phy_test_pattern_supported', 'dp_link_parse_phy_test_params',
                             'dp_catalog_ctrl_send_phy_pattern', 'dp_ctrl_send_phy_test_pattern'],
                  inputs={str(q.resolve()): sha(q) for q in files}, runs=runs, wiring_checks=wiring,
                  semantic_sha256=sha(a.output / 'semantic.txt') if (a.output / 'semantic.txt').exists() else None,
                  hardware_validated=False, full_module_validated=False)
    (a.output / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n')
    print(runs[-1]['stdout'], end='')
    print('RESULT', a.output / 'RESULT.json', result['status'])
    return 0 if result['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
