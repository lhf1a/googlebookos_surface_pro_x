"""BusyBox fixtures only; no real device/Chrome data and no cache-content backup."""
import hashlib
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT/'probe/graphics/chrome-graphics-cache.sh'
BB = shutil.which('busybox')

@unittest.skipUnless(BB, 'BusyBox required')
class ChromeGraphicsCache(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='chrome-only-fixture-')
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.root = self.base/'android'
        (self.root/'data').mkdir(parents=True)
        self.state = self.base/'state'
        self.installed = self.base/'installed'; self.installed.write_text('31\n109\n')
        self.unlocked = self.base/'unlocked'; self.unlocked.write_text('31\n109\n')
        for user in ('31','109'): (self.root/f'data/user/{user}').mkdir(parents=True)
        self.bin = self.base/'bin'; self.bin.mkdir()
        for name in subprocess.check_output([BB,'--list'],text=True).splitlines(): (self.bin/name).symlink_to(BB)

    def put(self, relative, content=b'fixture cache binary secret'):
        p=self.root/relative; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(content); return p

    def chrome(self, relative, *, user='31', area='user', store='data', content=b'fixture cache binary secret'):
        return self.put(f'{store}/{area}/{user}/com.android.chrome/{relative}',content)

    def invoke(self, mode='apply', *, expected=0, quiesced=True, source='mesa', target='qualcomm', extra=(), helper=None):
        args=[BB,'ash',str(helper or HELPER),mode,'--android-root',str(self.root),'--state-dir',str(self.state),
              '--from',source,'--to',target,'--installed-users',str(self.installed),'--ce-unlocked-users',str(self.unlocked),*extra]
        if quiesced: args.append('--quiesced')
        r=subprocess.run(args,capture_output=True,text=True,env=dict(os.environ,PATH=str(self.bin)),timeout=30)
        self.assertEqual(r.returncode,expected,r.stdout+r.stderr)
        tx=next((Path(line.split('=',1)[1]) for line in r.stdout.splitlines() if line.startswith('TRANSACTION=')),None)
        return r,tx

    def report(self, tx): return (tx/'report.tsv').read_text()

    def test_actual_chrome_layout_direct_deletion_no_backups_or_other_app_changes(self):
        self.chrome('app_chrome/Default/Preferences',content=b'{"private_setting":true}')
        graphics=[self.chrome('app_chrome/GPUPersistentCache/GPUCache/blob'),self.chrome('app_chrome/GPUPersistentCache/DawnGraphiteCache/blob')]
        graphics += [self.chrome(f'app_chrome/Default/{name}/blob') for name in ('GPUCache','DawnGraphiteCache','DawnWebGPUCache','ShaderCache')]
        graphics += [self.chrome('cache/com.android.skia.shaders_cache',area='user_de'),self.chrome('cache/com.android.opengl.shaders_cache.multifile/one',area='user_de')]
        private=[self.chrome('app_chrome/Default/'+n,content=('PRIVATE:'+n).encode()) for n in ('Cookies','History','Login Data','Preferences','Bookmarks')]
        private += [self.chrome('app_chrome/Default/Code Cache/js/item'),self.chrome('app_chrome/Downloads/GPUCache/user-file'),self.chrome('cache/http-response'),self.chrome('code_cache/js/script')]
        private += [self.chrome('app_chrome/GPUPersistentCache.before-sq2-chrome-20261102/GPUCache/old'),self.chrome('app_chrome/.graphics-backend-cache.interrupted/cache/GPUCache/old')]
        other=self.put('data/user/31/org.other.browser/app_chrome/Default/GPUCache/blob',b'other app untouched')
        residual=self.chrome('cache/com.android.opengl.shaders_cache',user='0')
        before={str(p):p.read_bytes() for p in private+[other,residual]}
        _,tx=self.invoke()
        for p in graphics: self.assertFalse(p.exists(),str(p))
        for p in private+[other,residual]: self.assertEqual(p.read_bytes(),before[str(p)])
        self.assertEqual((tx/'ready').read_text(),'target=qualcomm\ncoverage=chrome-graphics-only\ncache-backup=none\n')
        self.assertEqual((tx/'schema').read_text(),'chrome-graphics-cache-v1\n')
        self.assertEqual((tx/'cache-backup').read_text(),'none\n')
        self.assertFalse(list(self.root.rglob('.graphics-backend-cache.chrome-switch*')))
        for f in tx.rglob('*'):
            if f.is_file():
                self.assertNotIn(b'fixture cache binary secret',f.read_bytes())
                self.assertNotIn(b'PRIVATE:',f.read_bytes())
        self.assertNotIn('org.other.browser',self.report(tx))
        self.assertNotIn('org.other.browser'.encode().hex(),self.report(tx))
        self.assertNotIn('/user/0/',(tx/'target-roots').read_text())
        self.assertFalse(list(tx.glob('.walk.*')))

    def test_dry_run_never_deletes_or_readies_and_does_not_require_quiescence(self):
        p=self.chrome('app_chrome/GPUCache/blob');_,tx=self.invoke('dry-run',quiesced=False)
        self.assertTrue(p.exists());self.assertFalse((tx/'ready').exists());self.assertIn('WOULD_DELETE',self.report(tx))
        self.assertEqual((tx/'deleted-cache-count').read_text(),'0\n')

    def test_apply_requires_quiescence(self):
        p=self.chrome('app_chrome/GPUCache/blob');self.invoke(expected=64,quiesced=False)
        self.assertTrue(p.exists());self.assertFalse(self.state.exists())

    def test_other_browser_package_rejected_before_scanning(self):
        self.invoke(expected=64,extra=['--package','org.chromium.chrome']);self.assertFalse(self.state.exists())

    def test_locked_installed_user_pending_and_cache_retained(self):
        p=self.chrome('app_chrome/GPUCache/blob');self.unlocked.write_text('109\n')
        _,tx=self.invoke(expected=2);self.assertTrue(p.exists());self.assertFalse((tx/'ready').exists());self.assertIn('CE_DEFERRED',self.report(tx))

    def test_only_listed_installed_users_are_visited(self):
        p=self.chrome('app_chrome/GPUCache/blob',user='42');self.installed.write_text('')
        _,tx=self.invoke();self.assertTrue(p.exists());self.assertEqual((tx/'target-roots').read_text(),'')

    def test_dynamic_adopted_storage_and_profile_name(self):
        self.chrome('app_chrome/Work browser profile/Preferences',user='109',store='mnt/expand/volume-dynamic')
        p=self.chrome('app_chrome/Work browser profile/GPUPersistentCache/blob',user='109',store='mnt/expand/volume-dynamic')
        _,tx=self.invoke();self.assertFalse(p.exists());self.assertIn('volume-dynamic',(tx/'target-roots').read_text())

    def test_unverified_profile_and_unknown_graphics_are_pending_not_deleted(self):
        p=self.chrome('app_chrome/Default/GPUCache/blob');q=self.chrome('cache/custom-vulkan-pipeline.bin')
        _,tx=self.invoke(expected=2);self.assertTrue(p.exists());self.assertTrue(q.exists());self.assertFalse((tx/'ready').exists())
        self.assertIn('PROFILE_UNVERIFIED',self.report(tx));self.assertIn('UNKNOWN_GRAPHICS',self.report(tx))

    def test_symlink_profile_root_and_candidate_are_never_followed(self):
        outside=self.put('outside/GPUCache/blob',b'outside data')
        chrome=self.root/'data/user/31/com.android.chrome';chrome.mkdir()
        (chrome/'app_chrome').symlink_to(outside.parent.parent,target_is_directory=True)
        _,tx=self.invoke(expected=2);self.assertEqual(outside.read_bytes(),b'outside data');self.assertFalse((tx/'ready').exists())

    def test_symlink_inside_candidate_prevents_whole_candidate_deletion(self):
        p=self.chrome('app_chrome/GPUCache/blob');outside=self.put('elsewhere/file',b'untouched')
        (p.parent/'link').symlink_to(outside)
        _,tx=self.invoke(expected=2);self.assertTrue(p.exists());self.assertEqual(outside.read_bytes(),b'untouched');self.assertIn('CACHE_UNSAFE_MEMBER',self.report(tx))

    def test_nonregular_cache_member_prevents_deletion(self):
        p=self.chrome('app_chrome/GPUCache/blob');os.mkfifo(p.parent/'fifo')
        _,tx=self.invoke(expected=2);self.assertTrue(p.exists());self.assertIn('CACHE_UNSAFE_MEMBER',self.report(tx))

    def test_same_backend_does_not_inspect_or_create_state(self):
        self.invoke(source='mesa',target='mesa');self.assertFalse(self.state.exists())

    def test_reverse_transition_has_same_chrome_only_rules(self):
        p=self.chrome('app_chrome/GPUCache/blob');_,tx=self.invoke(source='qualcomm',target='mesa')
        self.assertFalse(p.exists());self.assertEqual((tx/'ready').read_text(),'target=mesa\ncoverage=chrome-graphics-only\ncache-backup=none\n')

    def test_invalid_user_input_cannot_be_interpreted_as_path_or_shell(self):
        for bad in ('31\n31\n','../other\n','$(touch x)\n','31\n\n'):
            self.installed.write_text(bad);self.invoke(expected=64);self.assertFalse(self.state.exists())

    def test_state_directory_cannot_be_inside_application_data(self):
        self.state=self.root/'data/user/31/com.android.chrome/state'
        self.state.parent.mkdir()
        self.invoke(expected=64);self.assertFalse(self.state.exists())

    def test_chrome_app_root_symlink_does_not_visit_other_application(self):
        other=self.put('data/user/31/org.other.app/app_chrome/GPUCache/blob',b'other app')
        (self.root/'data/user/31/com.android.chrome').symlink_to(other.parents[2],target_is_directory=True)
        _,tx=self.invoke(expected=2)
        self.assertEqual(other.read_bytes(),b'other app');self.assertEqual((tx/'target-roots').read_text(),'')

    def test_candidate_and_profile_symlinks_are_pending(self):
        outside=self.put('outside/Preferences',b'private outside')
        browser=self.root/'data/user/31/com.android.chrome/app_chrome';browser.mkdir(parents=True)
        (browser/'GPUCache').symlink_to(outside.parent,target_is_directory=True)
        (browser/'Profile 2').symlink_to(outside.parent,target_is_directory=True)
        _,tx=self.invoke(expected=2)
        self.assertEqual(outside.read_bytes(),b'private outside')
        self.assertIn('CACHE_ALIAS',self.report(tx));self.assertIn('PROFILE_ALIAS',self.report(tx))

    def test_delete_failure_is_pending_and_never_ready(self):
        p=self.chrome('app_chrome/GPUCache/blob')
        failed=self.base/'failed-helper.sh'
        failed.write_text(HELPER.read_text().replace('rm -rf -- "$path"','false',1))
        _,tx=self.invoke(expected=2,helper=failed)
        self.assertTrue(p.exists());self.assertFalse((tx/'ready').exists());self.assertIn('DELETE_FAILED',self.report(tx))

    def test_missing_ce_storage_is_not_certified(self):
        self.installed.write_text('77\n');self.unlocked.write_text('77\n')
        _,tx=self.invoke(expected=2);self.assertIn('CE_STORAGE_UNAVAILABLE',self.report(tx));self.assertFalse((tx/'ready').exists())

    def test_exact_external_chrome_root_keeps_downloads_and_ignores_other_apps(self):
        download=self.put('data/media/31/Android/data/com.android.chrome/files/Download/GPUCache/user-document',b'download content')
        other=self.put('data/media/31/Android/data/org.other.app/cache/com.android.skia.shaders_cache',b'other external cache')
        cache=self.put('mnt/expand/dynamic-volume/media/109/Android/data/com.android.chrome/cache/com.android.opengl.shaders_cache',b'graphics cache')
        _,tx=self.invoke()
        self.assertEqual(download.read_bytes(),b'download content');self.assertEqual(other.read_bytes(),b'other external cache')
        self.assertFalse(cache.exists())
        roots=(tx/'target-roots').read_text()
        self.assertIn('/data/media/31/Android/data/com.android.chrome',roots)
        self.assertIn('/mnt/expand/dynamic-volume/media/109/Android/data/com.android.chrome',roots)
        self.assertNotIn('org.other.app',roots)

    def test_nested_historical_backup_prevents_whole_candidate_delete(self):
        active=self.chrome('app_chrome/GPUCache/blob',content=b'active graphics cache')
        history=self.chrome('app_chrome/GPUCache/.graphics-backend-cache.previous/blob',content=b'historical content')
        _,tx=self.invoke(expected=2)
        self.assertEqual(active.read_bytes(),b'active graphics cache');self.assertEqual(history.read_bytes(),b'historical content')
        self.assertIn('CACHE_CONTAINS_HISTORY',self.report(tx));self.assertFalse((tx/'ready').exists())

    def test_mount_inside_profile_is_refused_before_traversal(self):
        self.chrome('app_chrome/Profile 2/Preferences')
        p=self.chrome('app_chrome/Profile 2/GPUCache/blob')
        fake=self.base/'mountinfo'
        mount=str(p.parent.parent).replace(' ','\\040')
        fake.write_text(f'10 1 0:1 / {mount} rw - ext4 fixture rw\n')
        helper=self.base/'mount-observer.sh';helper.write_text(HELPER.read_text().replace('/proc/self/mountinfo',str(fake)))
        _,tx=self.invoke(expected=2,helper=helper)
        self.assertTrue(p.exists());self.assertEqual((tx/'target-roots').read_text(),'')
        self.assertIn('CHROME_ROOT_MOUNT_BOUNDARY',self.report(tx))

    def test_no_content_reading_or_backup_operators_in_helper(self):
        text=HELPER.read_text()
        self.assertNotIn('sha256sum',text);self.assertNotIn('cp ',text);self.assertNotIn('tar ',text)
        self.assertNotIn('fingerprint',text)
        self.assertEqual(text.count('rm -rf -- "$path"'),1)
        self.assertNotIn('graphics-backend-cache.sh',text)

    @unittest.skipUnless(hasattr(os,'geteuid') and os.geteuid()==0,'isolated chroot fixture requires root')
    def test_actual_android_root_slash_in_isolated_chroot(self):
        binary=self.root/'bin/busybox';binary.parent.mkdir();shutil.copy2(BB,binary)
        linked=subprocess.run(['ldd',BB],capture_output=True,text=True)
        for library in re.findall(r'(/[^\s()]+)',linked.stdout):
            source=Path(library)
            if source.is_file():
                destination=self.root/library.lstrip('/');destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,destination)
        subprocess.run([str(binary),'--install','-s',str(binary.parent)],check=True)
        # Replace host-absolute applet links with chroot-visible links.
        for applet in binary.parent.iterdir():
            if applet.is_symlink(): applet.unlink();applet.symlink_to('/bin/busybox')
        shutil.copy2(HELPER,self.root/'helper.sh')
        (self.root/'installed').write_text('31\n');(self.root/'unlocked').write_text('31\n')
        (self.root/'proc/self').mkdir(parents=True);(self.root/'proc/self/mountinfo').write_text('')
        (self.root/'dev').mkdir();(self.root/'dev/null').touch()
        p=self.chrome('app_chrome/GPUCache/blob');private=self.chrome('app_chrome/Default/Preferences',content=b'private setting')
        command=[BB,'chroot',str(self.root),'/bin/busybox','env','PATH=/bin','/bin/busybox','ash','/helper.sh','apply',
                 '--android-root','/','--state-dir','/state','--from','mesa','--to','qualcomm','--installed-users','/installed','--ce-unlocked-users','/unlocked','--quiesced']
        result=subprocess.run(command,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr);self.assertFalse(p.exists());self.assertEqual(private.read_bytes(),b'private setting')
        tx=next((self.root/'state').iterdir());self.assertEqual((tx/'android-root').read_text(),'/\n')
        self.assertEqual((tx/'target-roots').read_text(),'/data/user/31/com.android.chrome\n')

if __name__=='__main__': unittest.main()
