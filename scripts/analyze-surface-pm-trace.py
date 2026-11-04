#!/usr/bin/env python3
"""Pair per-device PM trace callbacks, retaining errors, gaps and missing boundaries."""
import argparse
from collections import defaultdict
import csv
from decimal import Decimal
import hashlib
import json
from pathlib import Path
import re

LINE = re.compile(r'-(\d+)\s+\[\d+\]\s+\S+\s+(\d+\.\d+):\s+(device_pm_callback_start|device_pm_callback_end|suspend_resume): (.*)$')
START = re.compile(r'(\S*) (.+), parent: (.*), (.*?)\[([^]]+)\]$')
END = re.compile(r'(\S*) (.+), err=(-?\d+)$')
BOUNDARY = re.compile(r'(.+)\[(\d+)\] (begin|end)$')


def analyze(text):
    records, boundaries = [], []
    pending = defaultdict(list)
    phases = []
    for number, line in enumerate(text.splitlines(), 1):
        match = LINE.search(line)
        if not match:
            if not line.startswith('#') and re.search(r': (device_pm_callback_(?:start|end)|suspend_resume):', line):
                raise ValueError(f'unrecognized trace prefix on line {number}')
            continue
        tid, timestamp, event, data = match.groups()
        if event == 'suspend_resume':
            boundary = BOUNDARY.fullmatch(data)
            if not boundary:
                raise ValueError(f'unrecognized PM boundary on line {number}')
            action, value, edge = boundary.groups()
            boundaries.append(dict(time=timestamp, action=action, value=int(value), edge=edge))
            if action.startswith('dpm_'):
                if edge == 'begin':
                    phases.append(action)
                elif action in phases:
                    phases.remove(action)
            continue
        if event == 'device_pm_callback_start':
            start = START.fullmatch(data)
            if not start:
                raise ValueError(f'unrecognized callback start on line {number}')
            driver, device, parent, callback, transition = start.groups()
            record = dict(phase=phases[-1] if phases else 'outside_dpm_phase', driver=driver,
                          device=device, parent=parent, callback=callback.strip(), transition=transition, tid=int(tid),
                          start=timestamp, end='', duration_us='', error='', status='end_missing')
            records.append(record)
            pending[(tid, driver, device)].append(record)
        else:
            end = END.fullmatch(data)
            if not end:
                raise ValueError(f'unrecognized callback end on line {number}')
            driver, device, error = end.groups()
            stack = pending[(tid, driver, device)]
            if stack:
                record = stack.pop()
                duration = (Decimal(timestamp)-Decimal(record['start']))*1000000
                if duration < 0:
                    raise ValueError(f'callback time reversed on line {number}')
                record.update(end=timestamp, duration_us=int(duration), error=int(error), status='returned')
            else:
                records.append(dict(phase=phases[-1] if phases else 'outside_dpm_phase',
                                    driver=driver, device=device, parent='', callback='', transition='', tid=int(tid),
                                    start='', end=timestamp, duration_us='', error=int(error), status='start_missing'))
    header = re.search(r'entries-in-buffer/entries-written:\s*(\d+)/(\d+)', text)
    retained, written = map(int, header.groups()) if header else (None, None)
    gaps = retained != written or bool(re.search(r'\bLOST\s+\d+\s+EVENTS\b', text))
    counts = defaultdict(int)
    for record in records:
        counts[record['status']] += 1
    errors = [r for r in records if isinstance(r['error'], int) and r['error'] != 0]
    summary = dict(entries_retained=retained, entries_written=written, recorded_gaps=gaps,
                   header_present=header is not None, callbacks=dict(counts), errors=errors,
                   unique_driver_device_pairs=len({(r['driver'], r['device']) for r in records}),
                   phases=dict((phase, sum(r['phase'] == phase for r in records))
                               for phase in sorted({r['phase'] for r in records})),
                   slowest=sorted((r for r in records if r['status'] == 'returned'),
                                  key=lambda r: r['duration_us'], reverse=True)[:20],
                   boundaries=boundaries,
                   scope='Tracepoint observations only. prepare includes devices without a hardware callback; '
                         'a missing boundary is not proof of a hang, especially with trace gaps. '
                         'Successful PM debug callbacks do not establish real sleep or firmware low power.')
    return records, summary


def write_report(trace, output):
    raw = trace.read_bytes()
    records, summary = analyze(raw.decode('utf-8'))
    if not records:
        raise ValueError('no device PM callback records')
    output.mkdir(parents=True, exist_ok=False)
    summary['input_sha256'] = hashlib.sha256(raw).hexdigest()
    with (output/'callbacks.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(records[0]))
        writer.writeheader()
        writer.writerows(records)
    grouped = defaultdict(list)
    for record in records:
        grouped[(record['driver'], record['device'], record['parent'], record['phase'])].append(record)
    with (output/'components.csv').open('w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['driver', 'device', 'parent', 'phase', 'records', 'returned', 'errors', 'missing_boundaries', 'max_duration_us'])
        for (driver, device, parent, phase), items in sorted(grouped.items()):
            writer.writerow([driver, device, parent, phase, len(items),
                             sum(r['status'] == 'returned' for r in items),
                             sum(isinstance(r['error'], int) and r['error'] != 0 for r in items),
                             sum(r['status'] != 'returned' for r in items),
                             max((r['duration_us'] for r in items if r['status'] == 'returned'), default='')])
    (output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps({key: summary[key] for key in ('callbacks', 'unique_driver_device_pairs', 'recorded_gaps', 'header_present')}, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('output', type=Path)
    a = p.parse_args()
    write_report(a.trace, a.output)
