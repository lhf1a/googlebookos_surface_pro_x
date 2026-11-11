#!/usr/bin/env python3
"""Run real bus loader/voting code with fixed source policy and modeled leaves."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('extract', ROOT/'scripts/check-kgsl-dmabuf.py')
extractor = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extractor)
CASES = ['reference', 'row-reordered', 'off-first', 'indexed-ignores-peak', 'table-minimum', 'table-hfi-maximum', 'table-u32-maximum', 'non-native-policy', 'legacy-ok', 'unknown', 'empty-mode', 'multi-mode', 'empty-property',
         'unterminated-mode', 'read-mode-error', 'no-table', 'short-table', 'empty-table',
         'huge-table', 'malformed-table', 'read-table-error', 'table-off', 'table-equal',
         'table-decrease', 'no-parent', 'disabled-parent', 'duplicate-row', 'extra-duplicate', 'fuzzy-frequency', 'missing-row', 'extra-active',
         'extra-disabled', 'duplicate-off', 'missing-off', 'off-triple', 'min-zero',
         'default-zero', 'min-gt-default', 'default-gt-max', 'index-equal-count',
         'index-overflow', 'global-max', 'opp-count-extra', 'opp-count-missing', 'disabled-opp',
         'opp-count-error', 'opp-lookup-first', 'opp-lookup-last', 'opp-exact-mismatch',
         'opp-avg', 'opp-duplicate', 'opp-zero-freq', 'count-zero', 'count-too-big',
         'count-hfi', 'active-invalid', 'default-invalid', 'icc-names', 'icc-index',
         'alloc', 'icc-null', 'icc-defer', 'icc-error', 'already-owned', 'ten-target-frequencies']
CASES += [f'field-{field}-{kind}' for field in range(4) for kind in range(3)]
CASES += ['peak-'+name for name in ['ok', 'no-first', 'no-last', 'decreasing', 'overflow',
          'avg', 'opp-error', 'missing-opp', 'icc-names', 'icc-index', 'duplicate-freq',
          'alloc', 'icc-defer', 'icc-null', 'zero-count', 'count-big', 'active-big',
          'default-big', 'hfi-full']]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--source', type=Path, required=True, help='Exact kgsl_bus.c file')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--mutations', action='store_true')
    p.add_argument('--bus-regression-source', type=Path, help='Parent drivers/gpu/msm for unchanged 99-case harness')
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=False)
    source = a.source.resolve().read_text()
    body = re.sub(r'^#include[^\n]*\n', '', source, flags=re.M)
    harness = (ROOT/'tests/kgsl_indexed_policy_test.c').read_text()
    policy_path = ROOT/'configs/kgsl-sc8180x-downstream-policy-reference.json'
    policy_bytes = policy_path.read_bytes()
    (a.output/'REFERENCE-POLICY.json').write_bytes(policy_bytes)
    policy = json.loads(policy_bytes)
    rows = policy['frequency_bindings']
    assert len(rows) == 8 and len(policy['ddr_table_kBps']) == 13
    assert policy['captured_target_frequency_state_from_existing_project_record']['missing_bandwidth_bindings_hz'] == [590000000, 661000000, 700000000]
    header = 'static const u32 source_ddr[] = {' + ','.join(map(str, policy['ddr_table_kBps'])) + '};\n'
    header += 'static const struct fixture_row source_rows[] = {\n'
    header += ''.join('{%d,%d,%d,%d},\n' % (r['gpu_hz'], r['default_index'], r['min_index'], r['max_index']) for r in rows) + '};\n'
    (a.output/'source-policy.h').write_text(header)
    ledger = []
    def run(cmd):
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=30,
                           env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=1:halt_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1'})
        ledger.append(dict(command=list(map(str, cmd)), stdout=r.stdout, stderr=r.stderr, exit=r.returncode))
        (a.output/'LEDGER.json').write_text(json.dumps(ledger, indent=2)+'\n')
        return r
    def compile_run(name, text, cases):
        c = a.output/(name+'.c'); binary = a.output/name
        c.write_text(harness.replace('/* REAL_BUS_SOURCE */', text))
        command = ['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-Wno-unused-function', '-fsanitize=address,undefined',
                   '-fno-pie', '-no-pie', '-fno-omit-frame-pointer', '-I', str(a.output), str(c), '-o', str(binary)]
        r = run(command)
        if r.returncode:
            raise RuntimeError('compile failed: '+r.stderr)
        results = [run([str(binary), case]).returncode for case in cases]
        summary = dict(variant=name, compile_exit=0, passed=results.count(0), failed=len(results)-results.count(0))
        print(json.dumps(summary), flush=True)
        return summary
    summaries = [compile_run('actual', body, CASES)]
    if a.mutations:
        mutations = {
            'collapse-min': (body.replace('levels[i].bus_min = min;', 'levels[i].bus_min = 1;'), ['reference']),
            'collapse-default': (body.replace('levels[i].bus_freq = def;', 'levels[i].bus_freq = max;'), ['reference']),
            'ignore-duplicate': (body.replace('|| seen[i] ||', '|| false ||'), ['extra-duplicate']),
            'ignore-missing': (body.replace('if (!seen[i] || levels[i].bus_max > levels[0].bus_max)', 'if (levels[i].bus_max > levels[0].bus_max)'), ['missing-row']),
            'ignore-off': (body.replace('if (!off)\n\t\tgoto free_table;', 'if (false)\n\t\tgoto free_table;'), ['missing-off']),
            'ignore-global-max': (body.replace('|| levels[i].bus_max > levels[0].bus_max', '|| false'), ['global-max']),
            'early-table-publish': (body.replace('path = of_icc_get(&pdev->dev, "gfx-mem");', 'pwr->ddr_table = table;\n\tpath = of_icc_get(&pdev->dev, "gfx-mem");'), ['icc-defer']),
            'early-triples-publish': (body.replace('path = of_icc_get(&pdev->dev, "gfx-mem");', 'if (native) memcpy(pwr->pwrlevels, levels, sizeof(levels));\n\tpath = of_icc_get(&pdev->dev, "gfx-mem");'), ['icc-defer']),
            'ignore-mode': (body.replace('if (strcmp(mode, "indexed"))', 'if (false)'), ['unknown']),
            'frequency-fallback': (body.replace('freq == levels[i].gpu_freq', 'freq == levels[i].gpu_freq || freq + 1 == levels[i].gpu_freq'), ['fuzzy-frequency']),
            'bootstrap-default': (body.replace('level = pwr->pwrlevels[pwr->default_pwrlevel].bus_min;', 'level = pwr->pwrlevels[pwr->default_pwrlevel].bus_freq;'), ['reference']),
            'ignore-enabled-count': (body.replace('if ((unsigned int)ret != pwr->num_pwrlevels)', 'if (false)'), ['opp-count-extra']),
        }
        for name, (mutated, cases) in mutations.items():
            assert mutated != body, name+' did not change product code'
            summaries.append(compile_run(name, mutated, cases))
    if a.bus_regression_source:
        # Existing harness logic is unchanged; only unavailable-selector OF leaves
        # are added so its fixed 99 peak/legacy/AB/ICC tests can link new helpers.
        functions = []
        for filename, names in (
            ('adreno.c', ['adreno_interconnect_bus_set']),
            ('adreno_a6xx_gmu.c', ['a6xx_gmu_bus_set']),
            ('adreno_a6xx_hwsched.c', ['a6xx_hwsched_bus_set']),
            ('kgsl_bus.c', ['kgsl_bus_set_icc', '_ab_buslevel_update', 'kgsl_bus_update', 'validate_pwrlevels', 'kgsl_bus_get_opp_table',
                            'kgsl_bus_read_policy_u32', 'kgsl_bus_get_indexed_table', 'kgsl_bus_get_native_table', 'kgsl_bus_init', 'kgsl_bus_close']),
            ('kgsl_pwrctrl.c', ['kgsl_pwrctrl_axi']), ('kgsl_pwrscale.c', ['kgsl_busmon_target'])):
            t = source if filename == 'kgsl_bus.c' else (a.bus_regression_source/filename).read_text()
            for name in names:
                if name in ('kgsl_bus_read_policy_u32', 'kgsl_bus_get_indexed_table', 'kgsl_bus_get_native_table') and name+'(' not in t:
                    continue
                functions.append(extractor.extract(t, name))
        (a.output/'kgsl-bus-functions.h').write_text('\n\n'.join(functions))
        old = (ROOT/'tests/kgsl_bus_test.c').read_text()
        leaves = '''
static void *of_find_property(struct device_node *n,const char *s,int *l){return NULL;}
static int of_property_count_elems_of_size(struct device_node *n,const char *s,int z){return -EINVAL;}
static int of_property_read_u32(struct device_node *n,const char *s,u32 *v){return -EINVAL;}
static int of_property_read_string(struct device_node *n,const char *s,const char **v){return -EINVAL;}
static struct device_node *of_get_child_by_name(struct device_node *n,const char *s){return NULL;}
static bool of_device_is_available(struct device_node *n){return n!=NULL;}
static void of_node_put(struct device_node *n){}
#define for_each_available_child_of_node(p,c) for((c)=NULL;(c);(c)=NULL)
static int dev_pm_opp_get_opp_count(struct device *d){return opp_count;}
int kgsl_bus_set_icc(struct kgsl_device *device,u32 ab,u32 ib);
'''
        if 'static void *of_find_property(' in old:
            leaves = 'int kgsl_bus_set_icc(struct kgsl_device *device,u32 ab,u32 ib);\n'
        (a.output/'bus-regression.c').write_text(old.replace('#include "kgsl-bus-functions.h"', leaves+'\n#include "kgsl-bus-functions.h"'))
        binary = a.output/'bus-regression'
        c = run(['gcc', '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                 '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-pie', '-no-pie', '-I', str(a.output), str(a.output/'bus-regression.c'), '-o', str(binary)])
        if c.returncode:
            raise RuntimeError(c.stderr)
        r = run([str(binary)])
        print(r.stdout, end='', flush=True)
        if r.returncode:
            raise RuntimeError('existing bus regression failed: '+r.stderr)
    report = dict(source=str(a.source.resolve()), source_sha256=hashlib.sha256(a.source.read_bytes()).hexdigest(),
                  fixture=str(policy_path), fixture_snapshot=str(a.output/'REFERENCE-POLICY.json'), fixture_sha256=hashlib.sha256(policy_bytes).hexdigest(),
                  harness_sha256=hashlib.sha256((ROOT/'tests/kgsl_indexed_policy_test.c').read_bytes()).hexdigest(), summaries=summaries,
                  status='host-contract-only-not-calibrated-not-activated')
    (a.output/'RESULT.json').write_text(json.dumps(report, indent=2)+'\n')
    return int(summaries[0]['failed'] != 0 or any(s['failed'] == 0 for s in summaries[1:]))


if __name__ == '__main__':
    raise SystemExit(main())
