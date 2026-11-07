import importlib.util
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('profile_snapshot',Path(__file__).resolve().parents[1]/'scripts/profile_snapshot.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class ProfileSnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.base=Path(self.temp.name);self.profile=self.base/'profile';self.profile.mkdir()
        for name in ['active.db','registry.db','archives/2025.db']:
            path=self.profile/name;path.parent.mkdir(exist_ok=True)
            conn=sqlite3.connect(path)
            conn.execute('CREATE TABLE evidence(id INTEGER PRIMARY KEY, amount_cents INTEGER)')
            conn.execute('INSERT INTO evidence VALUES(1,12345)');conn.execute('PRAGMA user_version=7')
            conn.commit();conn.close()
        for name,data in [('settings.ini',b'local=true'),('attachments/invoice.pdf',b'test attachment'),('recovery/pending.json',b'{"phase":"routed"}'),('keys/test.key',b'synthetic-key')]:
            path=self.profile/name;path.parent.mkdir(exist_ok=True);path.write_bytes(data)
        self.backup=self.base/'snapshot'
    def test_full_profile_roundtrip(self):
        m.snapshot(self.profile,self.backup);manifest=m.verify(self.backup)
        self.assertEqual(len(manifest['files']),7)
        target=m.restore(self.backup,self.base/'restored')
        self.assertEqual(set(m.inventory(target)),set(m.inventory(self.profile)))
        for name,path in m.inventory(target).items():
            if name.endswith('.db'):
                conn=sqlite3.connect(path)
                try:self.assertEqual(conn.execute('SELECT * FROM evidence').fetchall(),[(1,12345)])
                finally:conn.close()
            else:self.assertEqual(path.read_bytes(),(self.profile/name).read_bytes())
    def test_refuses_existing_destination(self):
        m.snapshot(self.profile,self.backup)
        with self.assertRaises(FileExistsError):m.restore(self.backup,self.profile)
        self.assertEqual((self.profile/'settings.ini').read_bytes(),b'local=true')
    def test_rejects_tampered_attachment(self):
        m.snapshot(self.profile,self.backup)
        (self.backup/'files/attachments/invoice.pdf').write_bytes(b'changed')
        with self.assertRaises(ValueError):m.restore(self.backup,self.base/'restored')
        self.assertFalse((self.base/'restored').exists())
    def test_rejects_traversal(self):
        m.snapshot(self.profile,self.backup);path=self.backup/'manifest.json'
        doc=json.loads(path.read_text());doc['files'][0]['path']='../escape'
        path.write_text(json.dumps(doc))
        with self.assertRaises(ValueError):m.verify(self.backup)
    def test_unlisted_files_rejected(self):
        m.snapshot(self.profile,self.backup);(self.backup/'files/extra').write_text('unexpected')
        with self.assertRaises(ValueError):m.verify(self.backup)
    def test_backup_failure_is_not_published_and_retry_works(self):
        def crash(phase):
            if phase=='copied':raise RuntimeError('synthetic interruption')
        with self.assertRaises(RuntimeError):m.snapshot(self.profile,self.backup,_step=crash)
        self.assertFalse(self.backup.exists());m.snapshot(self.profile,self.backup);m.verify(self.backup)
    def test_restore_failure_does_not_publish(self):
        m.snapshot(self.profile,self.backup);target=self.base/'restored'
        def crash(phase):
            if phase=='copied':raise RuntimeError('synthetic interruption')
        with self.assertRaises(RuntimeError):m.restore(self.backup,target,_step=crash)
        self.assertFalse(target.exists());m.restore(self.backup,target)
    def test_settings_changed_during_copy_rejected(self):
        def edit(phase):
            if phase=='copied':(self.profile/'settings.ini').write_text('changed')
        with self.assertRaises(ValueError):m.snapshot(self.profile,self.backup,_step=edit)
        self.assertFalse(self.backup.exists())
    def test_busy_database_fails_without_publishing(self):
        conn=sqlite3.connect(self.profile/'active.db');conn.execute('BEGIN IMMEDIATE')
        try:
            with self.assertRaises(sqlite3.OperationalError):m.snapshot(self.profile,self.backup)
        finally:conn.rollback();conn.close()
        self.assertFalse(self.backup.exists())
    def test_wal_committed_content_is_in_snapshot(self):
        conn=sqlite3.connect(self.profile/'active.db');conn.execute('PRAGMA journal_mode=WAL')
        conn.execute('INSERT INTO evidence VALUES(2,50000)');conn.commit()
        try:m.snapshot(self.profile,self.backup)
        finally:conn.close()
        target=m.restore(self.backup,self.base/'restored');conn=sqlite3.connect(target/'active.db')
        try:self.assertEqual(conn.execute('SELECT SUM(amount_cents) FROM evidence').fetchone()[0],62345)
        finally:conn.close()
        self.assertFalse((target/'active.db-wal').exists())
    def test_process_crash_before_publish_is_recoverable(self):
        module=Path(m.__file__).resolve()
        code="import importlib.util,os,sys; s=importlib.util.spec_from_file_location('backup',sys.argv[1]); m=importlib.util.module_from_spec(s); s.loader.exec_module(m); m.snapshot(sys.argv[2],sys.argv[3],_step=lambda phase: os._exit(87) if phase=='verified' else None)"
        result=subprocess.run([sys.executable,'-B','-c',code,str(module),str(self.profile),str(self.backup)],capture_output=True,timeout=20)
        self.assertEqual(result.returncode,87,result.stderr)
        self.assertFalse(self.backup.exists())
        m.snapshot(self.profile,self.backup);m.restore(self.backup,self.base/'restored')

    def test_relocates_sql_ini_json_and_preserves_secrets(self):
        original=str(self.profile/'attachments/invoice.pdf').replace('\\','/')
        external=str(self.base/'external.pdf').replace('\\','/')
        with closing(sqlite3.connect(self.profile/'registry.db')) as conn:
            conn.execute('CREATE TABLE refs(id INTEGER PRIMARY KEY,path TEXT,payload TEXT,token BLOB)')
            conn.execute('INSERT INTO refs VALUES(1,?,?,?)',(original,json.dumps({'file':original,'external':external}),b'dpapi:opaque'))
            conn.commit()
        (self.profile/'settings.ini').write_text('[test]\nabsolute_attachment='+original+'\nsecret=@ByteArray(opaque)\n',encoding='utf-8')
        (self.profile/'recovery/pending.json').write_text(json.dumps({'attachment':original,'external':external}))
        m.snapshot(self.profile,self.backup)
        target=m.restore(self.backup,self.base/'restored')
        expected=str(self.base/'restored/attachments/invoice.pdf').replace('\\','/')
        # Settings store portable paths; extended Windows prefixes are IO syntax.
        if expected.startswith('//?/UNC/'):expected='//'+expected[8:]
        elif expected.startswith('//?/'):expected=expected[4:]
        with closing(m.connect(target/'registry.db')) as conn:
            row=conn.execute('SELECT path,payload,token FROM refs').fetchone()
        self.assertEqual(row[0],expected);self.assertEqual(json.loads(row[1]),{'file':expected,'external':external});self.assertEqual(row[2],b'dpapi:opaque')
        self.assertIn('absolute_attachment='+expected,(target/'settings.ini').read_text(encoding='utf-8'));self.assertIn('secret=@ByteArray(opaque)',(target/'settings.ini').read_text(encoding='utf-8'))
        self.assertEqual(json.loads((target/'recovery/pending.json').read_text())['attachment'],expected)
        m.verify(self.backup)
        with closing(sqlite3.connect(self.profile/'registry.db')) as conn:self.assertEqual(conn.execute('SELECT path FROM refs').fetchone()[0],original)

    def test_missing_internal_reference_blocks_publication(self):
        (self.profile/'settings.ini').write_text('path='+str(self.profile/'missing.pdf'),encoding='utf-8')
        m.snapshot(self.profile,self.backup)
        with self.assertRaisesRegex(ValueError,'Missing profile-owned'):m.restore(self.backup,self.base/'restored')
        self.assertFalse((self.base/'restored').exists())

    def test_similar_prefix_is_not_rebased(self):
        original=str(self.profile)+'-other/file.pdf'
        (self.profile/'settings.ini').write_text('path='+original,encoding='utf-8')
        m.snapshot(self.profile,self.backup);target=m.restore(self.backup,self.base/'restored')
        self.assertEqual((target/'settings.ini').read_text(encoding='utf-8'),'path='+original)

    def test_crash_after_relocation_does_not_publish(self):
        (self.profile/'settings.ini').write_text('path='+str(self.profile/'attachments/invoice.pdf'),encoding='utf-8')
        m.snapshot(self.profile,self.backup)
        def fail(phase):
            if phase=='relocated':raise RuntimeError('injected crash')
        with self.assertRaises(RuntimeError):m.restore(self.backup,self.base/'restored',_step=fail)
        self.assertFalse((self.base/'restored').exists())
        m.restore(self.backup,self.base/'restored')


    def test_ini_path_encodings_relocate_and_missing_paths_block(self):
        # Exercise literal Windows paths and QSettings escaping independently.
        for style in ('literal', 'escaped', 'forward'):
            for missing in (False, True):
                with self.subTest(style=style, missing=missing):
                    source=m.checked(self.profile/('missing.pdf' if missing else 'attachments/invoice.pdf'))
                    value=str(source)
                    if style=='escaped':value=value.replace('\\','\\\\')
                    if style=='forward':value=value.replace('\\','/')
                    (self.profile/'settings.ini').write_text('path="'+value+'"\nsecret=@ByteArray(opaque)\n',encoding='utf-8')
                    snapshot=self.base/('snapshot-'+style+str(missing))
                    target=self.base/('restored-'+style+str(missing))
                    m.snapshot(self.profile,snapshot)
                    if missing:
                        with self.assertRaisesRegex(ValueError,'Missing profile-owned'):
                            m.restore(snapshot,target)
                        self.assertFalse(target.exists())
                    else:
                        restored=m.restore(snapshot,target)
                        settings=(restored/'settings.ini').read_text(encoding='utf-8')
                        self.assertIn('secret=@ByteArray(opaque)',settings)
                        path=settings.splitlines()[0].split('=',1)[1].strip('"')
                        self.assertEqual(m.checked(Path(path)),m.checked(target/'attachments/invoice.pdf'))

    def test_long_snapshot_staging_path(self):
        # Generated stage suffix also counts toward Windows MAX_PATH.
        parent=m.checked(self.base/('a'*80)/('b'*80))
        parent.mkdir(parents=True)
        backup=parent/'snapshot'
        m.snapshot(self.profile,backup)
        target=m.restore(backup,parent/'restored')
        self.assertEqual(set(m.inventory(target)),set(m.inventory(self.profile)))

    def test_overlapping_roots_rejected(self):
        with self.assertRaises(ValueError):m.snapshot(self.profile,self.profile/'backup')
    def test_unknown_database_format_rejected(self):
        (self.profile/'encrypted.db').write_bytes(b'not plaintext sqlite')
        with self.assertRaises(ValueError):m.snapshot(self.profile,self.backup)

if __name__=='__main__':unittest.main(verbosity=2)
