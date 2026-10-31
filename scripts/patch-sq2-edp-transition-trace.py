#!/usr/bin/env python3
"""Add bounded observations to isolated V6; no extra AUX/MMIO operations.

Removing marked insertions must reproduce each pinned input exactly. This is
a diagnostic candidate, not a display fix or part of the default build.
"""
import hashlib
from pathlib import Path
import re
import sys

EXPECTED = {
    'dp_aux.c': '017d9c98ef73a63b0cb363aad672ba7e855267cd613d36da55f04391942a66d4',
    'dp_display.c': '8089bcc64babfafb15ca2184b115e0b7eabaad83bb317dce6c22a37bcf6c1e48',
    'dp_link.c': 'b3cb2124fbe16e266c26675e2ef7ca13e57164a686b0d66570e0c6998bf5cb4f',
}


def patch(stage):
    dp = Path(stage) / 'drivers/gpu/drm/msm/dp'
    original = {n: (dp / n).read_text() for n in EXPECTED}
    for n, digest in EXPECTED.items():
        if hashlib.sha256(original[n].encode()).hexdigest() != digest:
            raise ValueError('Expected pristine V6 source: ' + n)
    texts = original.copy()

    def add(n, anchor, code, after=False, function=None):
        s = texts[n]
        begin = s.index(function) if function else 0
        end = s.index('\n}', begin) + 2 if function else len(s)
        part = s[begin:end]
        if part.count(anchor) != 1:
            raise ValueError('Ambiguous insertion: ' + n + ' ' + anchor)
        block = '/* SQ2_TRACE_BEGIN */\n' + code + '/* SQ2_TRACE_END */\n'
        part = part.replace(anchor, anchor + block if after else block + anchor)
        texts[n] = s[:begin] + part + s[end:]

    add('dp_aux.c', '\tbool initted;\n', '\tu32 sq2_trace_count;\n', after=True)
    fn = 'static ssize_t dp_aux_transfer('
    add('dp_aux.c', '\tssize_t ret;\n', '\tconst char *sq2_phase = "not_initted";\n', True, fn)
    add('dp_aux.c', '\t\tret = dp_catalog_aux_wait_for_hpd_connect_state(aux->catalog);',
        '\t\tsq2_phase = "hpd_wait";\n', function=fn)
    add('dp_aux.c', '\tret = dp_aux_cmd_fifo_tx(aux, msg);',
        '\tsq2_phase = "fifo_tx";\n', function=fn)
    add('dp_aux.c', '\taux->cmd_busy = false;\n',
        '\tif (ret >= 0)\n\t\tsq2_phase = "completed";\n', function=fn)
    add('dp_aux.c', 'exit:\n', '''	if (aux->is_edp && (aux->native || ret < 0) && aux->sq2_trace_count < 256) {
		int bytes = ret > 0 && msg->reply == 0 ? min_t(size_t, ret, min_t(size_t, msg->size, 8)) : 0;
		aux->sq2_trace_count++;
		pr_info("SQ2_TRANS_AUX n=%u phase=%s addr=%x req=%x size=%zu ret=%zd reply=%x data=%*phN\\n",
			aux->sq2_trace_count, sq2_phase, msg->address, msg->request,
			msg->size, ret, msg->reply, bytes, msg->buffer);
	}
''', True, fn)

    add('dp_display.c', 'static int dp_add_event(', '''static void sq2_trace_display(struct dp_display_private *dp, const char *event)
{
	static atomic_t count = ATOMIC_INIT(0);
	if (dp->dp_display.connector_type == DRM_MODE_CONNECTOR_eDP &&
	    atomic_inc_return(&count) <= 128)
		pr_info("SQ2_TRANS_DISPLAY event=%s state=%u power=%u sink=%u rate=%u\\n",
			event, dp->hpd_state, dp->dp_display.power_on,
			dp->link->sink_count, dp->link->link_params.rate);
}

''')
    for name in ('dp_display_enable', 'dp_display_disable'):
        add('dp_display.c', '\tstruct msm_dp *dp_display = &dp->dp_display;\n',
            f'\tsq2_trace_display(dp, "{name}");\n', True, 'static int ' + name + '(')
    for name in ('dp_hpd_plug_handle', 'dp_hpd_unplug_handle', 'dp_irq_hpd_handle'):
        add('dp_display.c', '\tmutex_lock(&dp->event_mutex);\n',
            f'\tsq2_trace_display(dp, "{name}");\n', True, 'static int ' + name + '(')
    add('dp_display.c', '\tstruct edid *edid;\n',
        '\tsq2_trace_display(dp, "read_sink_caps_begin");\n', True,
        'static int dp_display_process_hpd_high(')
    add('dp_display.c', '\trc = dp_panel_read_sink_caps(dp->panel, dp->dp_display.connector);\n',
        '\tpr_info("SQ2_TRANS_CAPS rc=%d state=%u\\n", rc, dp->hpd_state);\n', True,
        'static int dp_display_process_hpd_high(')

    fn = 'int dp_link_psm_config('
    add('dp_link.c', '\tint ret = 0;\n',
        '\tstatic atomic_t sq2_count = ATOMIC_INIT(0);\n\tbool sq2_trace;\n', True, fn)
    add('dp_link.c', '\tmutex_lock(&link->psm_mutex);\n',
        '\tsq2_trace = atomic_inc_return(&sq2_count) <= 128;\n\tif (sq2_trace)\n\t\tpr_info("SQ2_TRANS_PSM begin enable=%u revision=%u\\n", enable, link_info->revision);\n', True, fn)
    add('dp_link.c', '\tmutex_unlock(&link->psm_mutex);',
        '\tif (sq2_trace)\n\t\tpr_info("SQ2_TRANS_PSM end enable=%u ret=%d\\n", enable, ret);\n', function=fn)
    for n, s in texts.items():
        restored = re.sub(r'/\* SQ2_TRACE_BEGIN \*/\n.*?/\* SQ2_TRACE_END \*/\n', '', s, flags=re.S)
        if restored != original[n]:
            raise ValueError('Non-observational source change: ' + n)
        (dp / n).write_text(s)


if __name__ == '__main__':
    patch(sys.argv[1])
    print('Trace insertions verified removable to exact V6 inputs; AUX limit256/display limit128')
