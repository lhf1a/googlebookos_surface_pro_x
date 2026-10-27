#!/usr/bin/env python3
"""Summarize passive getevent captures; observed input is not a hardware diagnosis."""
import argparse
import json
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('capture', type=Path)
args = parser.parse_args()
directory = args.capture
event_re = re.compile(r'\[\s*([\d.]+)\]\s+(EV_\w+)\s+(\w+)\s+(\w+)')
results = []
for line in (directory / 'captured-devices.txt').read_text().splitlines():
    event, name = line.split(' ', 1)
    rows = []
    for raw in (directory / f'{event}.txt').read_text().splitlines():
        match = event_re.search(raw)
        if match:
            timestamp, kind, code, value = match.groups()
            rows.append((float(timestamp), kind, code, value))
    axes = {}
    slot = 0
    active = set()
    pressed = None
    button_up_with_active_slots = 0
    for _, _, code, value in rows:
        if code == 'ABS_MT_SLOT':
            slot = int(value, 16)
        elif code == 'ABS_MT_TRACKING_ID':
            if value == 'ffffffff':
                active.discard(slot)
            else:
                active.add(slot)
        elif code == 'BTN_TOUCH':
            pressed = value in ('DOWN', '00000001')
        elif code == 'SYN_REPORT' and pressed is False and active:
            button_up_with_active_slots += 1
    for axis in ('ABS_MT_POSITION_X', 'ABS_MT_POSITION_Y', 'ABS_MT_TOUCH_MAJOR', 'ABS_MT_TOUCH_MINOR'):
        values = [int(v, 16) for _, _, c, v in rows if c == axis]
        if values:
            axes[axis] = {'min': min(values), 'max': max(values)}
    results.append({
        'event': event, 'name': name,
        'capture_exit': int((directory / f'{event}-exit-status.txt').read_text()),
        'events': len(rows),
        'contact_starts': sum(c == 'ABS_MT_TRACKING_ID' and v != 'ffffffff' for _, _, c, v in rows),
        'touch_down': sum(c == 'BTN_TOUCH' and v in ('DOWN', '00000001') for _, _, c, v in rows),
        'syn_dropped': sum(c == 'SYN_DROPPED' for _, _, c, _ in rows),
        'frames_button_up_with_observed_active_slots': button_up_with_active_slots,
        'reported_axis_ranges': axes,
        'first_event': rows[0][0] if rows else None,
        'last_event': rows[-1][0] if rows else None,
    })
report = {
    'capture': str(directory),
    'boot_id': (directory / 'boot-id.txt').read_text().strip(),
    'config': (directory / 'iptsd.conf').read_text(),
    'note': 'Counts require an operator-confirmed idle interval to identify unintended input. Axis ranges include only emitted changes, not every slot state.',
    'devices': results,
}
print(json.dumps(report, indent=2))
