#!/usr/bin/env python3
"""Check the real DSI GPIO callback signatures and unchanged dispatch semantics.

Extracts the product slot/declaration/body/stub and the paired kernel OF
declaration. OF/property leaves are modeled; this is not GPIO hardware validation.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def function(text, name):
    start = re.search(r'^(?:static inline )?int ' + name + r'\(', text, re.M)
    if not start:
        raise ValueError('Missing function: ' + name)
    brace = text.index('{', start.start())
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start.start():end]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', required=True, type=Path)
    p.add_argument('--kernel-source', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    a = p.parse_args()
    src = a.source.resolve()
    if (src / 'techpack/display/msm').is_dir():
        src /= 'techpack/display/msm'
    h = src / 'dsi/dsi_parser.h'
    c = src / 'dsi/dsi_parser.c'
    of = a.kernel_source.resolve() / 'include/linux/of_gpio.h'
    ht = h.read_text()
    slot = re.search(r'int \(\*get_named_gpio\)\([^;]+;', ht).group()
    decl = re.search(r'^int dsi_parser_get_named_gpio\([^;]+;', ht, re.M).group()
    ofdecl = re.search(r'extern int of_get_named_gpio\([^;]+;', of.read_text()).group()
    body = function(c.read_text(), 'dsi_parser_get_named_gpio')
    stub = function(ht[ht.index('#else /* CONFIG_DSI_PARSER */'):], 'dsi_parser_get_named_gpio')
    a.output = a.output.absolute()
    a.output.mkdir(parents=True, exist_ok=False)
    prelude = r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <assert.h>
typedef uint32_t u32;
struct device_node { int marker; };
static const struct device_node node = {123};
static const struct device_node *seen;
static const char *seen_name;
static int seen_index, value, read_error, calls;
static int dsi_parser_read_u32(const struct device_node *np, const char *name, u32 *out)
{
    seen=np; seen_name=name; calls++;
    if (!read_error) memcpy(out, &value, sizeof(value));
    return read_error;
}
'''
    # Parameter names come from the real OF declaration rather than assumed ABI.
    params = re.findall(r'\b([a-zA-Z_]\w*)\s*(?:,|\))', ofdecl)
    if len(params) != 3:
        raise ValueError('Unexpected paired OF declaration: ' + ofdecl)
    ofbody = ofdecl.replace('extern ', '').rstrip(';') + (
        '\n{ seen=%s; seen_name=%s; seen_index=%s; calls++; return value; }\n'
        % tuple(params))
    tail = r'''
int main(void)
{
    struct callbacks native={.get_named_gpio=of_get_named_gpio};
    struct callbacks parser={.get_named_gpio=dsi_parser_get_named_gpio};
    int values[]={0,1,1023,-2,-517,INT_MAX,INT_MIN,37};
    int indices[]={-1,0,1,3};
    unsigned cases=0;
    for (unsigned v=0;v<8;v++) for (unsigned i=0;i<4;i++) {
        value=values[v]; calls=0; seen=NULL; seen_name=NULL;
        assert(native.get_named_gpio(&node,"gpio",indices[i])==value);
        assert(calls==1 && seen==&node && !strcmp(seen_name,"gpio") && seen_index==indices[i]);
        calls=0;read_error=0;
#if ENABLED
        assert(parser.get_named_gpio(&node,"gpio",indices[i])==value);
        assert(calls==1 && seen==&node && !strcmp(seen_name,"gpio"));
        calls=0;read_error=-ENOENT;
        assert(parser.get_named_gpio(&node,"missing",indices[i])==-EINVAL && calls==1);
#else
        assert(parser.get_named_gpio(&node,"gpio",indices[i])==-ENODEV && calls==0);
#endif
        assert(node.marker==123);cases++;
    }
    printf("PASS %u GPIO callback cases enabled=%d; OF/property leaves modeled\n",cases,ENABLED);
    return 0;
}
'''
    runs = []
    for enabled in [1, 0]:
        code = prelude + '\n' + ofbody + '\nstruct callbacks {' + slot + '};\n'
        code += (decl + '\n' + body if enabled else stub) + '\n' + tail
        unit = a.output / ('probe%d.c' % enabled)
        unit.write_text(code)
        binary = a.output / ('probe%d' % enabled)
        commands = [['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                     '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-pointer-sign',
                     '-fsanitize=address,undefined', '-fno-pie', '-no-pie',
                     '-DENABLED=%d' % enabled, str(unit), '-o', str(binary)], [str(binary)]]
        for command in commands:
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            runs.append(dict(enabled=enabled, command=command, exit=result.returncode,
                             stdout=result.stdout, stderr=result.stderr))
            if result.returncode:
                break
    files = [h, c, of, Path(__file__)]
    result = dict(inputs={str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in files},
                  runs=runs, hardware_validated=False,
                  scope='Signature and modeled dispatch only; unchanged existing parser ignores index/read error and returns initialized EINVAL on failed read.')
    (a.output / 'RESULT.json').write_text(json.dumps(result, indent=2) + '\n')
    ok = len(runs) == 4 and all(r['exit'] == 0 for r in runs)
    print('GPIO const check:', 'pass' if ok else 'fail')
    return 0 if ok else 1


if __name__ == '__main__':
    raise SystemExit(main())
