#!/usr/bin/env python3
"""Track official Googlebook OS (mica) OTA updates and recovery image availability.

Compares live OTAChecker records and Google recovery CDN endpoints against the
pinned hop chain in configs/mica-ota-chain.json. Read-only network queries only.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_CHAIN = ROOT / 'configs/mica-ota-chain.json'
USER_AGENT = 'spx-mica-ota-tracker/1.0'


def load_chain(path=DEFAULT_CHAIN):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def parse_fingerprint(fp):
    """Parse Android fingerprint into dict(brand, product, device, release, build_id, incremental)."""
    m = re.match(r'^([^/]+)/([^/]+)/([^:]+):([^/]+)/([^/]+)/([^:]+):', str(fp or ''))
    if not m:
        return None
    return {
        'brand': m.group(1),
        'product': m.group(2),
        'device': m.group(3),
        'release': m.group(4),
        'build_id': m.group(5),
        'incremental': m.group(6),
    }


def _http_get(url, timeout=20):
    req = urllib.request.Request(url, headers={'User-Agent': USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return resp.read().decode('utf-8', errors='replace')


def _http_head(url, timeout=15):
    req = urllib.request.Request(url, method='HEAD', headers={'User-Agent': USER_AGENT})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return {
                'url': url,
                'status': resp.status,
                'content_length': int(resp.headers.get('Content-Length') or 0),
                'last_modified': resp.headers.get('Last-Modified', ''),
            }
    except urllib.error.HTTPError as exc:
        return {'url': url, 'status': exc.code, 'content_length': 0, 'last_modified': ''}
    except Exception as exc:
        return {'url': url, 'status': 0, 'error': str(exc)}


def fetch_otachecker_list(api_url, timeout=20):
    payload = json.loads(_http_get(api_url, timeout=timeout))
    return payload.get('data', [])


def fetch_otachecker_detail(ota_id, timeout=20):
    html = _http_get(f'https://otachecker.com/ota/{int(ota_id)}', timeout=timeout)
    for m in re.finditer(r'<script[^>]*data-sveltekit-fetched[^>]*>(.*?)</script>', html, re.S):
        try:
            outer = json.loads(m.group(1))
            body = json.loads(outer.get('body', '{}'))
            if isinstance(body, dict) and 'ota_info' in body:
                return body
        except Exception:
            continue
    return None


def analyze_chain_status(chain, live_entries=None, details_by_id=None, recovery_checks=None):
    pinned_ids = {int(h['otachecker_id']) for h in chain.get('hops', []) if 'otachecker_id' in h}
    pinned_builds = {chain['base']['build']}
    for h in chain.get('hops', []):
        pinned_builds.add(h['from_build'])
        pinned_builds.add(h['to_build'])
    target_hop = chain['hops'][-1] if chain.get('hops') else None
    target_build = target_hop['to_build'] if target_hop else chain['base']['build']
    target_build_id = target_hop['to_build_id'] if target_hop else chain['base']['build_id']

    discovered = []
    untracked = []
    for entry in (live_entries or []):
        ota_id = int(entry.get('id', 0))
        pre_fp = entry.get('fingerprint', '')
        pre_parsed = parse_fingerprint(pre_fp) or {}
        rec = {
            'id': ota_id,
            'pre_fingerprint': pre_fp,
            'pre_build': pre_parsed.get('incremental', ''),
            'pre_build_id': pre_parsed.get('build_id', ''),
            'size': int(entry.get('size', 0)),
            'incremental': bool(entry.get('incremental', False)),
            'found': entry.get('found', ''),
            'post_timestamp': entry.get('post_timestamp', ''),
            'pinned': ota_id in pinned_ids,
        }
        detail = (details_by_id or {}).get(ota_id)
        if detail and isinstance(detail.get('ota_info'), dict):
            oi = detail['ota_info']
            post = (oi.get('metadata') or {}).get('postcondition') or {}
            post_builds = post.get('build') or []
            rec['url'] = oi.get('url', '')
            rec['post_fingerprint'] = post_builds[0] if post_builds else ''
            rec['post_build'] = post.get('buildIncremental', '')
            rec['security_patch'] = post.get('securityPatchLevel', '')
        discovered.append(rec)
        pre_inc = pre_parsed.get('incremental', '')
        if ota_id not in pinned_ids and pre_inc not in pinned_builds:
            # Flag only updates from or beyond the current pinned target build.
            if pre_inc == target_build or (pre_inc.isdigit() and target_build.isdigit() and int(pre_inc) >= int(target_build)):
                untracked.append(rec)
        elif ota_id not in pinned_ids and pre_inc == target_build:
            untracked.append(rec)

    return {
        'board': chain['board'],
        'base_build': chain['base']['build'],
        'base_build_id': chain['base']['build_id'],
        'pinned_target_build': target_build,
        'pinned_target_build_id': target_build_id,
        'pinned_hop_count': len(chain.get('hops', [])),
        'pinned_total_ota_bytes': sum(int(h['zip_bytes']) for h in chain.get('hops', [])),
        'live_ota_count': len(discovered),
        'live_otas': discovered,
        'new_untracked_otas': untracked,
        'recovery_checks': recovery_checks or {},
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--chain', type=Path, default=DEFAULT_CHAIN)
    parser.add_argument('--offline', action='store_true', help='Skip live network checks; report pinned chain only')
    parser.add_argument('--details', action='store_true', help='Fetch per-OTA detail pages (rate-limited 1.5s/req)')
    parser.add_argument('--check-recovery', action='store_true', help='HEAD-check Google recovery.zip endpoints for each build')
    parser.add_argument('--json', action='store_true', help='Emit machine-readable JSON only')
    args = parser.parse_args(argv)

    chain = load_chain(args.chain)
    live_entries = []
    details_by_id = {}
    recovery_checks = {}

    if not args.offline:
        live_entries = fetch_otachecker_list(chain['tracker_api'])
        if args.details:
            for idx, entry in enumerate(live_entries):
                if idx > 0:
                    time.sleep(1.5)
                ota_id = int(entry['id'])
                details_by_id[ota_id] = fetch_otachecker_detail(ota_id)
        if args.check_recovery:
            builds = [chain['base']['build']] + [h['to_build'] for h in chain.get('hops', [])]
            tmpl = chain['recovery_url_template']
            for b in builds:
                recovery_checks[b] = _http_head(tmpl.format(build=b))

    report = analyze_chain_status(chain, live_entries, details_by_id, recovery_checks)
    if args.json:
        print(json.dumps(report, indent=2))
        return 0

    print(f"Board: {report['board']} | Base: {report['base_build_id']} ({report['base_build']}) -> Pinned Target: {report['pinned_target_build_id']} ({report['pinned_target_build']})")
    print(f"Pinned hops: {report['pinned_hop_count']} (total {report['pinned_total_ota_bytes'] / 1048576:.2f} MiB)")
    if not args.offline:
        print(f"Live OTAChecker records: {report['live_ota_count']} (untracked beyond chain: {len(report['new_untracked_otas'])})")
        for rec in report['live_otas']:
            tag = 'PINNED' if rec['pinned'] else 'NEW'
            post_info = f" -> {rec.get('post_build', '')}" if rec.get('post_build') else ''
            print(f"  [{tag:6s}] id={rec['id']} pre={rec['pre_build_id']}({rec['pre_build']}){post_info} size={rec['size']/1048576:.2f} MiB found={rec['found']}")
    for b, info in report['recovery_checks'].items():
        print(f"  recovery {b}: HTTP {info.get('status')} ({info.get('content_length', 0)} B)")
    return 0


if __name__ == '__main__':
    sys.exit(main())
