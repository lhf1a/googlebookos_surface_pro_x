"""BusyBox-only fixtures; never access real Android or host application data."""
import hashlib
import os
import re
import shlex
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT/'probe/graphics/graphics-backend-cache.sh'
BUSYBOX = shutil.which('busybox')


@unittest.skipUnless(BUSYBOX, 'BusyBox required to test actual target shell/applications')
class GraphicsBackendCache(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='graphics-cache-fixture-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)/'android root'
        (self.root/'data').mkdir(parents=True)
        self.state = Path(self.tmp.name)/'state'
        self.ce = Path(self.tmp.name)/'unlocked-users'
        self.ce.write_text('31\n42\n109\n')
        self.bin = Path(self.tmp.name)/'busybox-only'
        self.bin.mkdir()
        for applet in subprocess.check_output([BUSYBOX, '--list'], text=True).splitlines():
            (self.bin/applet).symlink_to(BUSYBOX)

    def put(self, relative, content=b'graphics-derived fixture'):
        p = self.root/relative
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(content)
        return p

    def cache(self, user='31', package='org.fixture.alpha', name='GPUCache', store='data', area='user'):
        p = self.put(f'{store}/{area}/{user}/{package}/app_engine/profiles/profile space/{name}/blob')
        return p.parent

    def invoke(self, mode, *, expected=0, transaction=None, unlocked=True, from_backend='mesa', to_backend='qualcomm', quiesced=True, classifications=None):
        command = [BUSYBOX, 'ash', str(HELPER), mode]
        if transaction:
            command += ['--transaction', str(transaction)]
        else:
            command += ['--android-root', str(self.root), '--state-dir', str(self.state),
                        '--from', from_backend, '--to', to_backend]
        if unlocked:
            command += ['--ce-unlocked-users', str(self.ce)]
        if quiesced:
            command += ['--quiesced']
        if classifications:
            command += ['--classification-file', str(classifications)]
        p = subprocess.run(command, capture_output=True, text=True, timeout=30,
                           env=dict(os.environ, PATH=str(self.bin)))
        self.assertEqual(p.returncode, expected, p.stdout+p.stderr)
        tx = next((Path(x.removeprefix('TRANSACTION=')) for x in p.stdout.splitlines() if x.startswith('TRANSACTION=')), None)
        return p, tx

    def reports(self, tx):
        return '\n'.join(p.read_text() for p in tx.glob('report.*') if p.is_file() and not p.name.endswith('.pending'))

    def tree_fingerprint(self, path):
        stream = bytearray()
        items = ['.'] + ['./'+str(p.relative_to(path)) for p in path.rglob('*')]
        for item in sorted(items, key=os.fsencode):
            p = path/item
            self.assertFalse(p.is_symlink())
            kind = 'DIR' if p.is_dir() else 'FILE'
            info = p.stat()
            stream.extend(f'{kind}\0{item}\0'.encode())
            stream.extend(f'{info.st_mode & 0o7777:o} {info.st_uid} {info.st_gid}\n'.encode())
            if kind == 'FILE':
                stream.extend(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  -\n'.encode())
        return hashlib.sha256(stream).hexdigest()

    def classification_file(self, *records):
        result = Path(self.tmp.name)/'classifications.tsv'
        lines = ['graphics-cache-classifications-v1']
        for purpose, path in records:
            kind = 'symlink-target-file-sha256' if path.is_symlink() else ('tree-fingerprint-v1' if path.is_dir() else 'file-sha256')
            digest = self.tree_fingerprint(path) if path.is_dir() else hashlib.sha256(path.read_bytes()).hexdigest()
            lines.append('\t'.join([purpose, kind, str(path).encode().hex(), digest, 'fixture:inspected-format-v1']))
        result.write_text('\n'.join(lines)+'\n')
        return result

    def test_evidence_classifies_sources_backup_and_derived_cache_with_rollback(self):
        source = self.put('data/user/31/org.fixture.alpha/files/resources/shader.vert', b'precision highp float;\nvoid main() {}')
        cache = self.put('data/user/42/net.fixture.dynamic/files/Saved/driver-vulkan-cache', b'confirmed graphics cache')
        backup = self.put('data/user/31/org.fixture.alpha/app_engine/GPUCache.before-history/shader-record', b'old backup').parent
        table = self.classification_file(('preserve-source-asset', source), ('rebuildable-cache', cache), ('preserve-inactive-backup', backup))
        _, dry = self.invoke('dry-run', classifications=table)
        self.assertTrue(cache.exists())
        self.assertFalse((dry/'ready').exists())
        _, tx = self.invoke('apply', classifications=table)
        self.assertTrue(source.exists())
        self.assertTrue((backup/'shader-record').exists())
        self.assertFalse(cache.exists())
        self.assertTrue((tx/'ready').exists())
        snapshots = list(tx.glob('classifications.*'))
        self.assertEqual(len(snapshots), 1)
        self.assertEqual(snapshots[0].read_bytes(), table.read_bytes())
        self.invoke('rollback', transaction=tx)
        self.assertEqual(cache.read_bytes(), b'confirmed graphics cache')

    def test_classification_hash_change_blocks_readiness_and_preserves(self):
        p = self.put('data/user/31/org.fixture.alpha/files/shader.vert', b'source before')
        table = self.classification_file(('preserve-source-asset', p))
        p.write_bytes(b'changed after inspection')
        _, tx = self.invoke('apply', classifications=table, expected=2)
        self.assertFalse((tx/'ready').exists())
        self.assertIn('CLASSIFICATION_HASH_MISMATCH', self.reports(tx))
        self.assertEqual(p.read_bytes(), b'changed after inspection')

    def test_application_state_has_distinct_preserve_purpose_not_source_asset(self):
        db = self.put('data/user/31/org.fixture.alpha/databases/sdk/pipeline.db', b'fixture inspected application state')
        setting = self.put('data/user/42/net.fixture.other/files/mmkv/flutter_vulkan_config', b'device_model\0vulkan_test_result')
        table = self.classification_file(('preserve-application-data', db), ('preserve-application-data', setting))
        _, tx = self.invoke('apply', classifications=table)
        self.assertTrue(db.exists())
        self.assertTrue(setting.exists())
        self.assertEqual(self.reports(tx).count('PRESERVED_APPLICATION_DATA'), 2)
        self.assertNotIn('PRESERVED_SOURCE_ASSET', self.reports(tx))
        self.assertTrue((tx/'ready').exists())

    def test_repeated_path_bytes_are_not_compressed_in_hex_evidence(self):
        p = self.put('data/user/31/org.fixture.'+'a'*96+'/files/shader.vert', b'void main() {}')
        table = self.classification_file(('preserve-source-asset', p))
        _, tx = self.invoke('apply', classifications=table)
        self.assertIn(str(p).encode().hex(), self.reports(tx))
        self.assertTrue((tx/'ready').exists())
        self.assertTrue(p.exists())

    def test_application_data_container_cannot_hide_unknown_graphics_child(self):
        item = self.put('data/user/31/org.fixture.alpha/files/ordinary-pipeline-container/custom-vulkan-cache', b'unknown')
        table = self.classification_file(('preserve-application-data', item.parent))
        _, tx = self.invoke('apply', classifications=table, expected=2)
        self.assertTrue(item.exists())
        self.assertFalse((tx/'ready').exists())
        self.assertIn('UNKNOWN_GRAPHICS_CANDIDATE', self.reports(tx))

    def test_classification_cannot_preserve_builtin_live_cache(self):
        cache = self.cache()
        table = self.classification_file(('preserve-inactive-backup', cache))
        _, tx = self.invoke('apply', classifications=table)
        self.assertFalse(cache.exists())
        self.assertNotIn('PRESERVED_INACTIVE_BACKUP', self.reports(tx))

    def test_source_directory_cannot_hide_unknown_child(self):
        leaf = self.put('data/user/31/org.fixture.alpha/files/xshader/custom-vulkan-pipeline.bin', b'unclassified binary')
        table = self.classification_file(('preserve-source-asset', leaf.parent))
        _, tx = self.invoke('apply', classifications=table, expected=2)
        self.assertTrue(leaf.exists())
        self.assertIn('UNKNOWN_GRAPHICS_CANDIDATE', self.reports(tx))
        self.assertFalse((tx/'ready').exists())

    def test_mixed_resource_directory_moves_only_hash_named_proven_binary(self):
        cache = self.put('data/user/31/org.fixture.alpha/files/resources/Shaders/02d8f9be.bin', b'inspected compiled driver binary')
        source = self.put('data/user/31/org.fixture.alpha/files/resources/Shaders/material.asset', b'packaged source asset')
        metadata = self.put('data/user/31/org.fixture.alpha/files/resources/Shaders/index.json', b'{"download":"source asset"}')
        table = self.classification_file(('preserve-application-data', cache.parent), ('rebuildable-cache', cache))
        _, tx = self.invoke('apply', classifications=table)
        self.assertFalse(cache.exists())
        self.assertEqual(source.read_bytes(), b'packaged source asset')
        self.assertEqual(metadata.read_bytes(), b'{"download":"source asset"}')
        self.assertTrue((tx/'ready').exists())
        self.invoke('rollback', transaction=tx)
        self.assertEqual(cache.read_bytes(), b'inspected compiled driver binary')

    def test_classification_index_parses_table_once_not_once_per_discovered_leaf(self):
        source = self.put('data/user/31/org.fixture.alpha/files/shader.vert', b'void main() {}')
        for n in range(60):
            self.put(f'data/user/31/org.fixture.alpha/files/ordinary/{n:04x}.dat', b'ordinary data')
        table = self.classification_file(('preserve-source-asset', source))
        # Retain a valid last record without LF; the one-time index must not drop it.
        table.write_text(table.read_text().rstrip('\n'))
        _, tx = self.invoke('apply', classifications=table)
        # BusyBox ash can directly dispatch internal applets, bypassing PATH
        # wrappers. Check the actual implementation plus the created index.
        self.assertEqual(HELPER.read_text().count("awk -F"), 1)
        self.assertTrue((tx/'ready').exists())
        self.assertEqual(len(list(tx.glob('classification-index.*'))), 1)

    def test_index_decoder_failure_occurs_before_any_cache_move(self):
        cache = self.cache()
        source = self.put('data/user/31/org.fixture.alpha/files/shader.vert', b'void main() {}')
        table = self.classification_file(('preserve-source-asset', source))
        faulted = Path(self.tmp.name)/'decoder-fault-helper.sh'
        faulted.write_text(HELPER.read_text().replace('xxd -r -p', 'false'))
        with mock.patch(__name__+'.HELPER', faulted):
            _, tx = self.invoke('apply', classifications=table, expected=64)
        self.assertTrue(cache.exists())
        self.assertFalse((tx/'ready').exists())
        self.assertIn('CLASSIFICATION_INDEX_FAILED', self.reports(tx))

    def test_source_directory_and_each_unknown_leaf_must_be_classified(self):
        leaf = self.put('data/user/31/org.fixture.alpha/files/xshader/fshader.frag', b'void main() {}')
        table = self.classification_file(('preserve-source-asset', leaf.parent), ('preserve-source-asset', leaf))
        _, tx = self.invoke('apply', classifications=table)
        self.assertTrue(leaf.exists())
        self.assertTrue((tx/'ready').exists())

    def test_classified_tree_mutation_remains_pending(self):
        leaf = self.put('data/user/31/org.fixture.alpha/files/GPUCache.old-evidence/blob')
        table = self.classification_file(('preserve-inactive-backup', leaf.parent))
        (leaf.parent/'new-file').write_bytes(b'unreviewed')
        _, tx = self.invoke('apply', classifications=table, expected=2)
        self.assertIn('CLASSIFICATION_HASH_MISMATCH', self.reports(tx))
        self.assertTrue(leaf.exists())

    def test_unknown_tree_prints_actual_fingerprint_without_granting_readiness(self):
        leaf = self.put('data/user/31/org.fixture.alpha/files/custom-shader-tree/blob', b'unreviewed format')
        expected = self.tree_fingerprint(leaf.parent)
        _, tx = self.invoke('dry-run', expected=2)
        self.assertIn('tree-fingerprint-v1='+expected, self.reports(tx))
        self.assertFalse((tx/'ready').exists())
        self.assertEqual(leaf.read_bytes(), b'unreviewed format')

    def test_classification_malformed_duplicate_or_shell_text_rejected_before_moves(self):
        p = self.cache()
        source = self.put('data/user/31/org.fixture.alpha/files/shader.vert')
        table = self.classification_file(('preserve-source-asset', source))
        valid = table.read_text()
        malformed = [valid+valid.splitlines()[1]+'\n', valid.replace('fixture:inspected-format-v1', '$(touch /not-executed)'),
                     valid.replace('file-sha256', 'eval'), valid.replace('\tfile-sha256\t', '\t\t'),
                     valid.replace('graphics-cache-classifications-v1', 'unknown-version')]
        for body in malformed:
            with self.subTest(body=body):
                table.write_text(body)
                _, tx = self.invoke('apply', classifications=table, expected=64)
                self.assertTrue(p.exists())
                self.assertFalse((tx/'ready').exists())
                self.assertIn('CLASSIFICATION_INPUT_INVALID', self.reports(tx))

    def test_pending_transaction_accepts_new_explicit_evidence_and_snapshots_it(self):
        source = self.put('data/user/31/org.fixture.alpha/files/shader.vert', b'void main() {}')
        _, tx = self.invoke('apply', expected=2)
        table = self.classification_file(('preserve-source-asset', source))
        self.invoke('apply', transaction=tx, classifications=table)
        self.assertTrue((tx/'ready').exists())
        self.assertTrue(source.exists())

    def test_source_apk_file_symlink_requires_explicit_hash_evidence(self):
        target = self.put('product/priv-app/DynamicService/DynamicService.apk', b'PK\x03\x04fixture source archive')
        link = self.root/'data/user/31/org.fixture.alpha/app_dynamic/module/DynamicService.apk'
        link.parent.mkdir(parents=True)
        link.symlink_to(target)
        _, before = self.invoke('dry-run', expected=2)
        self.assertIn('DIRECTORY_ALIAS_UNCOVERED', self.reports(before))
        table = self.classification_file(('preserve-source-asset', link))
        _, tx = self.invoke('apply', classifications=table)
        self.assertTrue((tx/'ready').exists())
        self.assertTrue(link.is_symlink())
        self.assertEqual(target.read_bytes(), b'PK\x03\x04fixture source archive')

    def test_file_symlink_evidence_cannot_accept_directory_or_hash_change(self):
        target = self.put('product/priv-app/DynamicService/Service.apk', b'PK\x03\x04source')
        link = self.root/'data/user/31/org.fixture.alpha/app_dynamic/Service.apk'
        link.parent.mkdir(parents=True)
        link.symlink_to(target)
        table = self.classification_file(('preserve-source-asset', link))
        target.write_bytes(b'changed')
        _, tx = self.invoke('apply', classifications=table, expected=2)
        self.assertIn('CLASSIFICATION_HASH_MISMATCH', self.reports(tx))
        link.unlink()
        link.symlink_to(target.parent, target_is_directory=True)
        _, tx = self.invoke('apply', classifications=table, expected=2)
        self.assertIn('CLASSIFICATION_UNREADABLE_OR_TYPE_MISMATCH', self.reports(tx))
        self.assertFalse((tx/'ready').exists())

    def test_graphics_file_link_never_silently_treated_as_source(self):
        target = self.put('product/derived-cache', b'actual derived cache')
        link = self.root/'data/user/31/org.fixture.alpha/code_cache/GPUCache'
        link.parent.mkdir(parents=True)
        link.symlink_to(target)
        table = self.classification_file(('preserve-source-asset', link))
        _, tx = self.invoke('apply', classifications=table, expected=2)
        self.assertIn('DIRECTORY_ALIAS_UNCOVERED', self.reports(tx))
        self.assertNotIn('PRESERVED_SOURCE_ASSET', self.reports(tx))
        self.assertTrue(target.exists())

    def test_symlink_digest_cannot_authorize_cache_move(self):
        target = self.put('product/source-resource', b'original')
        link = self.root/'data/user/31/org.fixture.alpha/files/custom-shader-cache'
        link.parent.mkdir(parents=True)
        link.symlink_to(target)
        table = self.classification_file(('rebuildable-cache', link))
        _, tx = self.invoke('apply', classifications=table, expected=64)
        self.assertIn('CLASSIFICATION_INPUT_INVALID', self.reports(tx))
        self.assertTrue(link.is_symlink())
        self.assertEqual(target.read_bytes(), b'original')

    def test_same_backend_does_not_inspect_or_create_state(self):
        path = self.cache()
        p, tx = self.invoke('apply', from_backend='mesa', to_backend='mesa')
        self.assertIn('NOOP backend unchanged', p.stdout)
        self.assertTrue(path.exists())
        self.assertFalse(self.state.exists())
        self.assertIsNone(tx)

    def test_dry_run_preserves_contents_and_never_issues_ready(self):
        path = self.cache()
        before = (path/'blob').read_bytes()
        _, tx = self.invoke('dry-run', quiesced=False)
        self.assertEqual((path/'blob').read_bytes(), before)
        self.assertIn('WOULD_MOVE', self.reports(tx))
        self.assertFalse((tx/'ready').exists())
        self.assertFalse(list(self.root.rglob('.graphics-backend-cache.*')))

    def test_all_dynamic_users_packages_adopted_external_and_hash_rollback(self):
        paths = [self.cache('31', name='GPUPersistentCache'),
                 self.cache('42', package='org.fixture.other', name='DawnGraphiteCache'),
                 self.cache('109', package='org.fixture.profile', name='DawnWebGPUCache', store='mnt/expand/volume-random'),
                 self.cache('42', package='net.fixture.browser', name='GPUCache', area='user_de'),
                 self.cache('42', package='net.fixture.browser', name='mesa_shader_cache_db'),
                 self.put('data/user/31/org.fixture.alpha/code_cache/com.android.skia.shaders_cache'),
                 self.put('data/media/31/Android/data/net.fixture.game/cache/com.android.opengl.shaders_cache'),
                 self.put('mnt/media_rw/public-volume/Android/data/net.fixture.game/cache/com.android.opengl.shaders_cache')]
        credentials = self.put('data/user/42/org.fixture.other/files/Cookies', b'private data retained')
        paths[0].chmod(0o750)
        before = {str(p): p.stat().st_mode for p in paths}
        _, tx = self.invoke('apply')
        self.assertTrue((tx/'ready').is_file())
        self.assertEqual((tx/'status').read_text().strip(), 'complete')
        self.assertTrue(all(not p.exists() for p in paths))
        self.assertEqual(credentials.read_bytes(), b'private data retained')
        self.assertEqual(len(list((tx/'entries').iterdir())), len(paths))
        for entry in (tx/'entries').iterdir():
            backup, source = Path((entry/'backup').read_text().strip()), Path((entry/'source').read_text().strip())
            self.assertEqual(backup.parent, source.parent)
            self.assertTrue(backup.exists())
        self.invoke('rollback', transaction=tx)
        self.assertTrue(all(p.exists() for p in paths))
        self.assertEqual(before, {str(p): p.stat().st_mode for p in paths})
        self.assertEqual((tx/'status').read_text().strip(), 'rolled-back')
        self.assertFalse((tx/'ready').exists())

    def test_locked_ce_is_deferred_and_resume_finishes_same_transition(self):
        ce = self.cache()
        de = self.cache(area='user_de', name='com.android.opengl.shaders_cache.multifile')
        _, tx = self.invoke('apply', unlocked=False, expected=2)
        self.assertTrue(ce.exists())
        self.assertFalse(de.exists())
        self.assertFalse((tx/'ready').exists())
        self.assertIn('CE_DEFERRED', self.reports(tx))
        self.invoke('apply', transaction=tx)
        self.assertFalse(ce.exists())
        self.assertTrue((tx/'ready').exists())

    def test_completed_transition_does_not_clean_new_backend_cache_again(self):
        p = self.cache()
        _, tx = self.invoke('apply')
        (p/'new').parent.mkdir()
        (p/'new').write_bytes(b'new backend data')
        out, _ = self.invoke('apply', transaction=tx)
        self.assertIn('NOOP transition already complete', out.stdout)
        self.assertEqual((p/'new').read_bytes(), b'new backend data')

    def sync_observer(self, fail_at=0):
        """Test-only function interception: observe ordering, not power-loss proof."""
        trace = Path(self.tmp.name)/'sync-order.txt'
        observed = Path(self.tmp.name)/'sync-observed-helper.sh'
        hook = r'''
SYNC_TEST_COUNT=0
sync() {
    SYNC_TEST_COUNT=$((SYNC_TEST_COUNT+1))
    present=0; saved=0; metadata=0; moved=0
    [ ! -e "$source" ] || present=1
    [ ! -e "$backup" ] || saved=1
    if [ -s "$entry/source" ] && [ -s "$entry/backup" ] && [ -s "$entry/sha256" ]; then metadata=1; fi
    if grep -q '^MOVED[[:space:]]' "$REPORT"; then moved=1; fi
    printf '%s|%s|%s|%s|%s|%s\n' "$SYNC_TEST_COUNT" "$present" "$saved" "$metadata" "$(cat "$entry/status")" "$moved" >> TRACE_PATH
    [ "$SYNC_TEST_COUNT" -ne FAIL_AT ]
}
'''.replace('TRACE_PATH', shlex.quote(str(trace))).replace('FAIL_AT', str(fail_at))
        observed.write_text(HELPER.read_text().replace('set -eu\n', 'set -eu\n'+hook, 1))
        return observed, trace

    def test_apply_syncs_journal_before_rename_and_completion_after_verification(self):
        self.cache()
        observed, trace = self.sync_observer()
        with mock.patch(__name__+'.HELPER', observed):
            result, tx = self.invoke('apply')
        self.assertEqual(trace.read_text().splitlines(), [
            '1|1|0|1|prepared|0', '2|0|1|1|prepared|0', '3|0|1|1|moved|1'])
        self.assertTrue((tx/'ready').exists())
        stages = [line.split('\t')[1] for line in result.stderr.splitlines() if line.startswith('PROGRESS\t')]
        self.assertEqual(stages, ['APP_BEGIN', 'CACHE_PREFLIGHT', 'CACHE_JOURNAL_SYNCED',
                                  'CACHE_RENAME_SYNCED', 'CACHE_MOVED_SYNCED', 'APP_END'])

    def assert_sync_failure(self, fail_at, expected_report):
        cache = self.cache()
        observed, trace = self.sync_observer(fail_at)
        with mock.patch(__name__+'.HELPER', observed):
            _, tx = self.invoke('apply', expected=74)
        self.assertEqual(len(trace.read_text().splitlines()), fail_at)
        self.assertFalse((tx/'ready').exists())
        self.assertIn(expected_report, self.reports(tx))
        entry = next((tx/'entries').iterdir())
        backup = Path((entry/'backup').read_text().strip())
        if fail_at == 1:
            self.assertTrue(cache.exists())
            self.assertFalse(backup.exists())
            retained = cache
        else:
            self.assertFalse(cache.exists())
            self.assertTrue(backup.exists())
            retained = backup
        self.assertEqual((retained/'blob').read_bytes(), b'graphics-derived fixture')

    def test_journal_sync_failure_never_renames(self):
        self.assert_sync_failure(1, 'JOURNAL_SYNC_FAILED')

    def test_post_rename_sync_failure_retains_backup_and_blocks_ready(self):
        self.assert_sync_failure(2, 'RENAMED_BACKUP_SYNC_FAILED')

    def test_completion_sync_failure_blocks_ready_without_discarding_backup(self):
        self.assert_sync_failure(3, 'MOVED_JOURNAL_SYNC_FAILED')

    def test_dry_run_reports_progress_but_never_syncs(self):
        cache = self.cache()
        observed, trace = self.sync_observer(1)
        with mock.patch(__name__+'.HELPER', observed):
            result, tx = self.invoke('dry-run')
        self.assertFalse(trace.exists())
        self.assertTrue(cache.exists())
        self.assertFalse((tx/'ready').exists())
        self.assertIn('PROGRESS\tCACHE_WOULD_MOVE\t', result.stderr)
        self.assertNotIn('SYNCED', result.stderr)

    def test_process_interruptions_at_each_sync_keep_recovery_metadata(self):
        # SIGKILL only the temporary helper child. This is process-interruption
        # coverage, not a claim to have emulated storage power loss.
        for stop_at in (1, 2, 3):
            with self.subTest(stop_at=stop_at):
                fixture = GraphicsBackendCache()
                fixture.setUp()
                try:
                    cache = fixture.cache()
                    observed, _ = fixture.sync_observer()
                    observed.write_text(observed.read_text().replace(
                        '[ "$SYNC_TEST_COUNT" -ne 0 ]',
                        f'[ "$SYNC_TEST_COUNT" -ne {stop_at} ] || kill -KILL $$\n    return 0'))
                    with mock.patch(__name__+'.HELPER', observed):
                        _, tx = fixture.invoke('apply', expected=-9)
                    entry = next((tx/'entries').iterdir())
                    for name in ('source', 'backup', 'sha256', 'status'):
                        self.assertTrue((entry/name).read_bytes(), name)
                    backup = Path((entry/'backup').read_text().strip())
                    retained = cache if stop_at == 1 else backup
                    self.assertEqual((retained/'blob').read_bytes(), b'graphics-derived fixture')
                    self.assertEqual((entry/'status').read_text().strip(), 'moved' if stop_at == 3 else 'prepared')
                    self.assertTrue((tx/'lock').exists())
                    self.assertFalse((tx/'ready').exists())
                finally:
                    fixture.doCleanups()

    def test_classified_cache_journal_failures_never_authorize_ready(self):
        # All failures run through `if classify_unknown`, where ash suppresses
        # implicit errexit for the nested function chain.
        cases = [
            ('path-write', 'printf \'%s\\n\' "$source" > "$entry/source"', 'false > "$entry/source"', False),
            ('path-empty-success', 'printf \'%s\\n\' "$source" > "$entry/source"', ': > "$entry/source"', False),
            ('guard-write', 'printf \'%s\\n\' "$guard_hash" > "$entry/classification-sha256"', 'false > "$entry/classification-sha256"', False),
            ('sha-write', 'printf \'%s\\n\' "$actual" > "$entry/sha256"', 'false > "$entry/sha256"', False),
            ('prepared-write', 'echo prepared > "$entry/status"', 'false > "$entry/status"', False),
            ('moved-write', 'echo moved > "$entry/status.next"', 'false > "$entry/status.next"', True),
            ('report-write', 'report MOVED "$source" "$actual";', 'false;', True),
        ]
        for label, original, fault, renamed in cases:
            with self.subTest(label=label):
                fixture = GraphicsBackendCache()
                fixture.setUp()
                try:
                    cache = fixture.put('data/user/31/org.fixture.alpha/files/proven-driver-binary', b'proven binary fixture')
                    table = fixture.classification_file(('rebuildable-cache', cache))
                    faulted = Path(fixture.tmp.name)/'faulted-helper.sh'
                    source = HELPER.read_text()
                    self.assertIn(original, source)
                    faulted.write_text(source.replace(original, fault, 1))
                    with mock.patch(__name__+'.HELPER', faulted):
                        _, tx = fixture.invoke('apply', expected=74, classifications=table)
                    entry = next((tx/'entries').iterdir())
                    backups = list(cache.parent.glob('.graphics-backend-cache.*'))
                    self.assertFalse((tx/'ready').exists())
                    if renamed:
                        self.assertFalse(cache.exists())
                        self.assertEqual(len(backups), 1)
                        self.assertEqual(backups[0].read_bytes(), b'proven binary fixture')
                        if label == 'moved-write':
                            self.assertEqual((entry/'status').read_text(), 'prepared\n')
                    else:
                        self.assertEqual(cache.read_bytes(), b'proven binary fixture')
                        self.assertEqual(backups, [])
                finally:
                    fixture.doCleanups()

    def test_classified_cache_rejects_failed_or_escaped_journal_directory(self):
        for failure in ('mktemp-failed', 'outside-entry-root', 'symlink-entry'):
            with self.subTest(failure=failure):
                fixture = GraphicsBackendCache()
                fixture.setUp()
                try:
                    cache = fixture.put('data/user/31/org.fixture.alpha/files/proven-driver-binary', b'proven binary fixture')
                    table = fixture.classification_file(('rebuildable-cache', cache))
                    foreign = Path(fixture.tmp.name)/'foreign-journal'
                    foreign.mkdir()
                    faulted = Path(fixture.tmp.name)/'faulted-helper.sh'
                    command = 'false'
                    if failure == 'outside-entry-root':
                        command = "printf '%s\\n' " + shlex.quote(str(foreign))
                    elif failure == 'symlink-entry':
                        command = 'ln -s ' + shlex.quote(str(foreign)) + ' "$TXN/entries/cache.alias"; printf \'%s\\n\' "$TXN/entries/cache.alias"'
                    faulted.write_text(HELPER.read_text().replace('mktemp -d "$TXN/entries/cache.XXXXXXXX"', command, 1))
                    with mock.patch(__name__+'.HELPER', faulted):
                        _, tx = fixture.invoke('apply', expected=74, classifications=table)
                    self.assertEqual(cache.read_bytes(), b'proven binary fixture')
                    self.assertEqual(list(foreign.iterdir()), [])
                    self.assertFalse((tx/'ready').exists())
                    self.assertEqual(list(cache.parent.glob('.graphics-backend-cache.*')), [])
                finally:
                    fixture.doCleanups()

    def test_classified_sync_failure_exits_even_when_pending_cannot_be_written(self):
        for fail_at in (1, 2, 3):
            with self.subTest(fail_at=fail_at):
                fixture = GraphicsBackendCache()
                fixture.setUp()
                try:
                    cache = fixture.put('data/user/31/org.fixture.alpha/files/proven-driver-binary', b'proven binary fixture')
                    table = fixture.classification_file(('rebuildable-cache', cache))
                    observed, _ = fixture.sync_observer(fail_at)
                    observed.write_text(observed.read_text().replace(
                        'pending() { report "$@"; touch "$PENDING"; }', 'pending() { return 1; }', 1))
                    with mock.patch(__name__+'.HELPER', observed):
                        _, tx = fixture.invoke('apply', expected=74, classifications=table)
                    self.assertFalse((tx/'ready').exists())
                    self.assertEqual(list(tx.glob('report.*.pending')), [])
                    entry = next((tx/'entries').iterdir())
                    backup = Path((entry/'backup').read_text().strip())
                    retained = cache if fail_at == 1 else backup
                    self.assertEqual(retained.read_bytes(), b'proven binary fixture')
                finally:
                    fixture.doCleanups()

    def test_rollback_never_overwrites_new_cache_and_can_resume_after_conflict(self):
        p = self.cache()
        _, tx = self.invoke('apply')
        p.mkdir()
        (p/'new').write_bytes(b'new backend data')
        self.invoke('rollback', transaction=tx, expected=2)
        self.assertEqual((p/'new').read_bytes(), b'new backend data')
        p.rename(p.with_name('retained-new-data'))
        self.invoke('rollback', transaction=tx)
        self.assertEqual((p/'blob').read_bytes(), b'graphics-derived fixture')

    def test_unknown_graphics_data_and_non_graphics_cache_are_preserved(self):
        self.cache()
        unknown = self.put('data/user/31/org.fixture.alpha/cache/custom-vulkan-pipeline.bin', b'unknown')
        ordinary = self.put('data/user/31/org.fixture.alpha/cache/http-response', b'ordinary')
        _, tx = self.invoke('apply', expected=2)
        self.assertEqual(unknown.read_bytes(), b'unknown')
        self.assertEqual(ordinary.read_bytes(), b'ordinary')
        self.assertIn('UNKNOWN_GRAPHICS_CANDIDATE', self.reports(tx))
        self.assertFalse((tx/'ready').exists())

    def test_graphics_symlink_is_not_followed(self):
        external = Path(self.tmp.name)/'must-not-read-or-move'
        external.mkdir()
        (external/'secret').write_bytes(b'secret')
        app = self.root/'data/user/31/org.fixture.alpha'
        app.mkdir(parents=True)
        (app/'cache').mkdir()
        (app/'cache/GPUCache').symlink_to(external, target_is_directory=True)
        _, tx = self.invoke('apply', expected=2)
        self.assertEqual((external/'secret').read_bytes(), b'secret')
        self.assertTrue((app/'cache/GPUCache').is_symlink())
        self.assertIn('GRAPHICS_LINK_NOT_FOLLOWED', self.reports(tx))

    def test_dynamic_user_root_alias_and_app_alias_are_covered_without_fixed_user(self):
        target = self.root/'data/actual-credential-store'
        target.mkdir()
        userroot = self.root/'data/user/31'
        userroot.parent.mkdir()
        userroot.symlink_to(target, target_is_directory=True)
        p = self.put('data/actual-credential-store/org.fixture.alias/cache/GPUCache/blob')
        actual = self.cache('42', package='org.fixture.shared')
        (target/'org.fixture.shared').symlink_to(actual.parents[3], target_is_directory=True)
        _, tx = self.invoke('apply')
        self.assertFalse(p.exists())
        self.assertFalse(actual.exists())
        self.assertIn('USER_STORAGE_ALIAS', self.reports(tx))
        self.assertIn('APP_ALIAS_TARGET_COVERED', self.reports(tx))
        self.invoke('rollback', transaction=tx)
        self.assertTrue(p.exists())

    def test_package_alias_outside_discovered_app_stores_prevents_ready(self):
        self.cache()
        external = Path(self.tmp.name)/'outside'
        external.mkdir()
        (self.root/'data/user/31/org.fixture.uncovered').symlink_to(external, target_is_directory=True)
        _, tx = self.invoke('apply', expected=2)
        self.assertFalse((tx/'ready').exists())
        self.assertIn('APP_ALIAS_UNCOVERED', self.reports(tx))

    def test_matching_graphics_name_in_user_documents_is_not_cache_proof(self):
        document = self.put('data/user/31/org.fixture.alpha/files/documents/ShaderCache/account.txt', b'not a cache')
        _, tx = self.invoke('apply', expected=2)
        self.assertEqual(document.read_bytes(), b'not a cache')
        self.assertIn('UNKNOWN_GRAPHICS_CANDIDATE', self.reports(tx))
        self.assertFalse((tx/'ready').exists())

    def test_changed_backup_refuses_restore(self):
        p = self.cache()
        _, tx = self.invoke('apply')
        entry = next((tx/'entries').iterdir())
        backup = Path((entry/'backup').read_text().strip())
        (backup/'blob').write_bytes(b'tampered fixture')
        self.invoke('rollback', transaction=tx, expected=2)
        self.assertFalse(p.exists())
        self.assertIn('BACKUP_HASH_MISMATCH', self.reports(tx))

    def test_intermediate_cache_and_engine_profile_symlinks_prevent_ready(self):
        for relative in ('cache', 'app_engine/Default'):
            with self.subTest(relative=relative):
                app = self.root/('data/user/31/org.fixture.'+relative.split('/')[0])
                path = app/relative
                path.parent.mkdir(parents=True, exist_ok=True)
                external = Path(self.tmp.name)/('external-'+relative.split('/')[0])
                (external/'GPUCache').mkdir(parents=True)
                (external/'GPUCache/blob').write_bytes(b'must be preserved')
                path.symlink_to(external, target_is_directory=True)
                _, tx = self.invoke('apply', expected=2)
                self.assertFalse((tx/'ready').exists())
                self.assertEqual((external/'GPUCache/blob').read_bytes(), b'must be preserved')
                self.assertIn('DIRECTORY_ALIAS_UNCOVERED', self.reports(tx))

    def test_rollback_recovers_if_move_succeeded_before_status_write(self):
        p = self.cache()
        _, tx = self.invoke('apply')
        entry = next((tx/'entries').iterdir())
        backup = Path((entry/'backup').read_text().strip())
        backup.rename(p)  # Simulate the atomic restore completed before process interruption.
        self.assertEqual((entry/'status').read_text().strip(), 'moved')
        self.invoke('rollback', transaction=tx)
        self.assertEqual((entry/'status').read_text().strip(), 'restored')
        self.assertEqual((p/'blob').read_bytes(), b'graphics-derived fixture')

    def test_exact_compositor_caches_are_reversible_without_clearing_misc(self):
        self.cache()
        skia = self.put('data/misc/surfaceflinger/skia_shaders', b'skia-derived')
        egl = self.put('data/misc/surfaceflinger/egl_shaders.multifile/blob', b'egl-derived').parent
        other = self.put('data/misc/surfaceflinger/unrelated', b'keep')
        _, tx = self.invoke('apply')
        self.assertFalse(skia.exists())
        self.assertFalse(egl.exists())
        self.assertEqual(other.read_bytes(), b'keep')
        self.invoke('rollback', transaction=tx)
        self.assertEqual(skia.read_bytes(), b'skia-derived')
        self.assertEqual((egl/'blob').read_bytes(), b'egl-derived')

    def test_missing_user_ce_record_prevents_ready(self):
        self.cache()
        self.put('data/system/users/88.xml', b'<user id="88"/>')
        _, tx = self.invoke('apply', expected=2)
        self.assertFalse((tx/'ready').exists())
        self.assertIn('user=88', self.reports(tx))

    def test_empty_unmounted_style_data_is_not_certified(self):
        _, tx = self.invoke('apply', expected=2)
        self.assertIn('NO_USER_STORAGE_OBSERVED', self.reports(tx))
        self.assertFalse((tx/'ready').exists())

    def test_mutation_requires_explicit_consumer_stop_attestation(self):
        p = self.cache()
        self.invoke('apply', quiesced=False, expected=64)
        self.assertTrue(p.exists())
        self.assertFalse(self.state.exists())

    def test_reverse_backend_transition_uses_same_rules(self):
        p = self.cache()
        _, tx = self.invoke('apply', from_backend='qualcomm', to_backend='mesa')
        self.assertFalse(p.exists())
        self.assertIn('target=mesa', (tx/'ready').read_text())

    @unittest.skipUnless(hasattr(os, 'geteuid') and os.geteuid() == 0, 'isolated chroot fixture requires root')
    def test_android_root_slash_in_isolated_chroot(self):
        p = self.cache()
        binary = self.root/'bin/busybox'
        binary.parent.mkdir()
        shutil.copyfile(BUSYBOX, binary)
        binary.chmod(0o755)
        deps = subprocess.run(['ldd', BUSYBOX], capture_output=True, text=True)
        for library in re.findall(r'(/\S+)\s+\(', deps.stdout):
            destination = self.root/library.lstrip('/')
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(library, destination)
        for applet in subprocess.check_output([BUSYBOX, '--list'], text=True).splitlines():
            if applet != 'busybox':
                (binary.parent/applet).symlink_to('busybox')
        shutil.copyfile(HELPER, self.root/'helper.sh')
        (self.root/'dev').mkdir()
        (self.root/'dev/null').touch()  # Fixture-only output sink; no device mounts required.
        (self.root/'users').write_text('31\n')
        command = [BUSYBOX, 'chroot', str(self.root), '/bin/busybox', 'ash', '/helper.sh', 'apply',
                   '--android-root', '/', '--state-dir', '/state', '--from', 'mesa', '--to', 'qualcomm',
                   '--ce-unlocked-users', '/users', '--quiesced']
        result = subprocess.run(command, capture_output=True, text=True, env=dict(os.environ, PATH='/bin'))
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertFalse(p.exists())
        tx = next((self.root/'state').iterdir())
        self.assertEqual((tx/'android-root').read_text(), '/\n')
        self.assertTrue((tx/'ready').exists())

    @unittest.skipIf(hasattr(os, 'geteuid') and os.geteuid() == 0, 'root can bypass permission fixture')
    def test_unreadable_app_is_reported_not_claimed_clean(self):
        p = self.cache()
        app = self.root/'data/user/31/org.fixture.alpha'
        app.chmod(0)
        try:
            _, tx = self.invoke('apply', expected=2)
            self.assertIn('APP_SCAN_INCOMPLETE', self.reports(tx))
            self.assertFalse((tx/'ready').exists())
        finally:
            app.chmod(0o700)
        self.assertTrue(p.exists())


if __name__ == '__main__':
    unittest.main()
