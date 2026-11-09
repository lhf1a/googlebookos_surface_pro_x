#!/usr/bin/env python3
"""Compile actual static registration to prove bind precedes HFI publication."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import resource
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('omx_patch', ROOT/'scripts/patch-sq2-venus-omx.py')
patch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patch)
HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#define VIDC_SESSION_TYPE_DEC 1
#define VIDC_SESSION_TYPE_ENC 2
#define IS_SQ2_HFI(c) ((c)->sq2)
struct device {int unused;};
struct lease {int bound;};
struct plane {struct lease *mem_priv;};
struct vb2_buffer {struct plane planes[1];};
struct vb2_v4l2_buffer {struct vb2_buffer vb2_buf;};
struct venus_buffer {struct vb2_v4l2_buffer vb;struct venus_buffer *next;};
struct venus_core {struct device *dev;int sq2;};
struct venus_inst {struct venus_core *core;int session_type;struct venus_buffer *registeredbufs;};
struct hfi_buffer_desc {struct lease *lease;};
static int dynamic,bind_calls,set_calls,bind_error,set_error,error_logs;
#define list_for_each_entry(buf,head,member) for(buf=*(head);buf;buf=buf->next)
static bool is_dynamic_bufmode(struct venus_inst *inst){assert(inst);return dynamic;}
static void fill_buffer_desc(struct venus_buffer *buf,struct hfi_buffer_desc *bd,bool response){
 assert(!response);bd->lease=buf->vb.vb2_buf.planes[0].mem_priv;
}
static int __attribute__((unused)) sq2_venus_scope_bind(struct venus_inst *inst,struct lease *lease){
 assert(inst->core->sq2 && inst->session_type==VIDC_SESSION_TYPE_DEC);bind_calls++;
 if(bind_error)return bind_error;
 lease->bound=1;return 0;
}
static int hfi_session_set_buffers(struct venus_inst *inst,struct hfi_buffer_desc *bd){
 set_calls++;
 if(inst->core->sq2 && inst->session_type==VIDC_SESSION_TYPE_DEC)assert(bd->lease->bound);
 return set_error;
}
static void dev_err(struct device *dev,const char *format,const char *name){
 (void)format;(void)name;assert(dev);error_logs++;
}
@FUNCTION@
int main(void){
 struct device dev={0};struct venus_core core={.dev=&dev,.sq2=1};
 struct lease a={0},b={0};struct venus_buffer second={.vb.vb2_buf.planes={{&b}}};
 struct venus_buffer first={.vb.vb2_buf.planes={{&a}},.next=&second};
 struct venus_inst inst={.core=&core,.session_type=VIDC_SESSION_TYPE_DEC,.registeredbufs=&first};
 assert(!session_register_bufs(&inst));assert(bind_calls==2 && set_calls==2 && a.bound && b.bound);
 bind_calls=set_calls=0;bind_error=-EIO;a.bound=b.bound=0;
 assert(session_register_bufs(&inst)==-EIO && bind_calls==1 && !set_calls && !a.bound && !b.bound);
 bind_calls=0;bind_error=0;set_error=-ETIMEDOUT;
 assert(session_register_bufs(&inst)==-ETIMEDOUT && bind_calls==1 && set_calls==1 && a.bound && !b.bound && error_logs==1);
 bind_calls=set_calls=0;set_error=0;core.sq2=0;
 assert(!session_register_bufs(&inst) && !bind_calls && set_calls==2);
 bind_calls=set_calls=0;core.sq2=1;inst.session_type=VIDC_SESSION_TYPE_ENC;
 assert(!session_register_bufs(&inst) && !bind_calls && set_calls==2);
 bind_calls=set_calls=0;inst.session_type=VIDC_SESSION_TYPE_DEC;dynamic=1;
 assert(!session_register_bufs(&inst) && !bind_calls && !set_calls);
 puts("STATIC_REGISTRATION_SCOPE_PASS cases=6");return 0;
}
"""



def audit_publication(directory, helpers, wrapper):
    """Account for every decoder-side HFI address publication call in actual sources."""
    decoder = (directory/'vdec.c').read_text()
    records = []
    covered = {'helpers.c': set(), 'vdec.c': set()}
    def function(source, name):
        matches = list(re.finditer(r'^(?:static )?(?:int\s+)?'+name+r'\(', source, re.M))
        if len(matches) != 1:
            raise ValueError('Unknown publication function ' + name)
        begin = matches[0].start()
        end = source.index('\n}', begin) + 2
        return begin, source[begin:end]
    for name, bind, publish, source_name in [
        ('venus_helper_queue_dpb_bufs', 'sq2_venus_scope_bind(inst, &buf->sq2_retired)',
         'hfi_session_process_buf(inst, &fdata)', 'helpers.c'),
        ('intbufs_set_buffer', 'sq2_venus_scope_bind(inst, &buf->sq2_retired)',
         'hfi_session_set_buffers(inst, &bd)', 'helpers.c'),
        ('session_process_buf', 'sq2_venus_scope_bind(inst, vb->planes[0].mem_priv)',
         'hfi_session_process_buf(inst, &fdata)', 'helpers.c'),
        ('session_register_bufs', 'sq2_venus_scope_bind(inst, buf->vb.vb2_buf.planes[0].mem_priv)',
         'hfi_session_set_buffers(inst, &bd)', 'helpers.c'),
    ]:
        start, body = function(helpers, name)
        assert body.count(publish) == 1
        pub = body.index(publish)
        ok = body.count(bind) == 1 and body.index(bind) < pub
        if ok:
            between = body[body.index(bind)+len(bind):pub]
            ok = bool(re.search(r'if \(ret\)\s*(?:return ret;|goto dma_free;|break;)', between))
        if name == 'session_process_buf':
            extra = 'sq2_venus_scope_bind(inst, vb->planes[1].mem_priv)'
            ok = ok and body.count(extra) == 1 and body.index(extra) < pub
            ok = ok and 'if (ret) return ret;' in body[body.index(extra):pub]
        if name == 'session_register_bufs':
            ok = ok and 'IS_SQ2_HFI(core) && inst->session_type == VIDC_SESSION_TYPE_DEC' in body
        covered[source_name].add(start+pub)
        records.append({'source': source_name, 'function': name,
                        'publication_line': helpers.count('\n', 0, start+pub)+1,
                        'scope_bound_before_publication_with_error_exit': bool(ok)})
    start, body = function(decoder, 'vdec_decoder_cmd')
    call = 'hfi_session_process_buf(inst, &fdata)'
    assert body.count(call) == 1
    covered['vdec.c'].add(start+body.index(call))
    eos = ('struct hfi_frame_data fdata = {0};' in body and
           'fdata.flags |= HFI_BUFFERFLAG_EOS;' in body and
           'fdata.device_addr = 0xdeadb000;' in body and
           'vb->planes' not in body and 'buf->dma_addr' not in body and
           'fdata.filled_len' not in body)
    records.append({'source': 'vdec.c', 'function': 'vdec_decoder_cmd',
                    'zero_length_eos_not_vb2_publication': eos})
    for name, text in [('helpers.c', helpers), ('vdec.c', decoder)]:
        calls = {m.start() for m in re.finditer(r'\bhfi_session_(?:process_buf|set_buffers)\(', text)}
        assert calls == covered[name], 'Unaccounted address publication in ' + name
    check = ('if (!lease->retired.scope) sq2_vb2_release(&lease->retired);\n'
             ' else sq2_venus_retire(lease->core, &lease->retired);') in wrapper
    return {'records': records, 'all_calls_accounted': True,
            'vb2_only_unbound_release': check,
            'all_pass': check and eos and all(row.get('scope_bound_before_publication_with_error_exit', True)
                                            for row in records),
            'helpers_sha256': hashlib.sha256(helpers.encode()).hexdigest(),
            'decoder_sha256': hashlib.sha256(decoder.encode()).hexdigest()}


def run(source, output):
    original = (source/'helpers.c').read_text()
    output.mkdir(parents=True, exist_ok=False)
    candidate = output/'candidate'
    candidate.mkdir()
    (candidate/'helpers.c').write_text(original)
    patch.apply_unsubmitted_capture(candidate, ROOT)
    fixed = (candidate/'helpers.c').read_text()
    publication = audit_publication(source, fixed, (candidate/'venus-sq2-vb2.h').read_text())
    (output/'SOURCE-PUBLICATION.json').write_text(json.dumps(publication, indent=2)+'\n')
    assert publication['all_pass']
    results = {}
    def no_core():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    for name, text in [('old', original), ('fixed', fixed)]:
        begin = text.index('static int session_register_bufs(')
        end = text.index('\n}', begin) + 2
        cfile, binary = output/(name+'.c'), output/name
        cfile.write_text(HARNESS.replace('@FUNCTION@', text[begin:end]))
        command = ['cc', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                   '-fno-pie', '-no-pie', str(cfile), '-o', str(binary)]
        compiled = subprocess.run(command, capture_output=True, text=True)
        (output/(name+'-build.txt')).write_text(compiled.stdout+compiled.stderr)
        compiled.check_returncode()
        result = subprocess.run([str(binary)], capture_output=True, text=True,
                                preexec_fn=no_core, timeout=10)
        (output/(name+'-run.txt')).write_text(result.stdout+result.stderr)
        results[name] = {'command': command, 'exit': result.returncode,
                         'source_sha256': hashlib.sha256(text.encode()).hexdigest()}
    (output/'RESULT.json').write_text(json.dumps(results, indent=2)+'\n')
    assert results['old']['exit'] != 0 and 'bd->lease->bound' in (output/'old-run.txt').read_text()
    assert results['fixed']['exit'] == 0
    print('STATIC_REGISTRATION_SCOPE_PASS old_publication_gap_detected=1 cases=6')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    run(args.source, args.output)
