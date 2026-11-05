#!/usr/bin/env python3
"""Real loopback HTTP, temporary source/SQLite, no NAS/network outside loopback."""
import base64
import hashlib
import http.client
import importlib.util
import os
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('index_server', ROOT/'tools/nas-indexdienst/server.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class ServerTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.source = self.root/'source'
        self.source.mkdir()
        (self.source/'empty').mkdir()
        (self.source/'ä file.txt').write_bytes(b'abc')
        self.server = m.make_server(self.source, self.root/'state', 'user', 'test-only', '/bernina/', ('127.0.0.1', 0))
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.auth = 'Basic ' + base64.b64encode(b'user:test-only').decode()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.server.index.db.close()
        self.tmp.cleanup()

    def request(self, method='GET', path='.bernina-index-v1', headers=None, auth=True):
        conn = http.client.HTTPConnection(*self.server.server_address, timeout=10)
        h = {'Authorization': self.auth} if auth else {}
        h.update(headers or {})
        conn.request(method, '/bernina/'+path, headers=h)
        response = conn.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        conn.close()
        return result

    def test_manifest_download_and_preconditions(self):
        status, headers, data = self.request()
        self.assertEqual(status, 200)
        token = headers['X-Bernina-Generation']
        self.assertIn(b'D\t0\t-\tempty\n', data)
        self.assertIn(b'%C3%A4%20file.txt', data)
        self.assertTrue(data.endswith(f'END\t2\t{token}\n'.encode()))
        digest = hashlib.sha256(b'abc').hexdigest()
        status, h, body = self.request(path='%C3%A4%20file.txt', headers={'If-Match': '"'+digest+'"', 'X-Bernina-Generation': token})
        self.assertEqual((status, body), (200, b'abc'))
        self.assertEqual(h['Digest'], 'sha-256='+base64.b64encode(bytes.fromhex(digest)).decode())
        self.assertEqual(self.request(path='%C3%A4%20file.txt', headers={'If-Match': '"wrong"'})[0], 412)
        self.assertEqual(self.request('HEAD', path='missing', headers={'X-Bernina-Generation': token})[0], 404)
        self.assertEqual(self.request('PROPFIND', '', {'Depth': '1'})[0], 207)
        self.assertEqual(self.request(auth=False)[0], 401)
        self.assertEqual(self.request(path='../state/index.sqlite')[0], 400)
        self.assertEqual(self.request('PUT', 'file')[0], 501)

    def test_same_size_preserved_mtime_and_generation(self):
        token = self.request()[1]['X-Bernina-Generation']
        p = self.source/'ä file.txt'
        st = p.stat()
        p.write_bytes(b'xyz')
        os.utime(p, ns=(st.st_atime_ns, st.st_mtime_ns))
        status, h, data = self.request()
        self.assertEqual(status, 200)
        self.assertNotEqual(h['X-Bernina-Generation'], token)
        self.assertIn(hashlib.sha256(b'xyz').hexdigest().encode(), data)
        self.assertEqual(self.request('HEAD', 'missing', {'X-Bernina-Generation': token})[0], 409)
        self.assertEqual(self.request(headers={'X-Bernina-Generation': token})[0], 409)

    def test_rename_delete_external_and_restart(self):
        first = self.request()[1]['X-Bernina-Generation']
        (self.source/'ä file.txt').rename(self.source/'new')
        (self.source/'empty').rmdir()
        (self.source/'nested').mkdir()
        (self.source/'nested'/'file').write_bytes(b'new')
        status, h, data = self.request()
        self.assertEqual(status, 200)
        self.assertNotEqual(first, h['X-Bernina-Generation'])
        self.assertIn(b'nested/file', data)
        other = m.Index(self.source, self.root/'state')
        self.assertEqual(other.refresh(), h['X-Bernina-Generation'])
        other.db.close()
        lost = m.Index(self.source, self.root/'new-state')
        self.assertNotEqual(lost.refresh(), h['X-Bernina-Generation'])
        lost.db.close()

    def test_fail_closed_source_and_race(self):
        token = self.request()[1]['X-Bernina-Generation']
        (self.source/'link').symlink_to(self.root/'state')
        self.assertEqual(self.request()[0], 503)
        self.assertEqual(self.server.index.token, token)
        (self.source/'link').unlink()
        original = self.server.index.enumerate
        calls = 0
        def race():
            nonlocal calls
            result = original()
            calls += 1
            if calls == 2:
                result['unexpected'] = []
            return result
        with patch.object(self.server.index, 'enumerate', race):
            self.assertEqual(self.request()[0], 503)
        self.assertEqual(self.server.index.token, token)
        self.source.rename(self.root/'old-source')
        self.source.mkdir()
        self.assertEqual(self.request()[0], 503)

    def test_corrupt_digest_and_disk_failure(self):
        self.request()
        with patch.object(self.server.index, 'digest', return_value='0'*64):
            self.assertEqual(self.request(path='%C3%A4%20file.txt')[0], 409)
        with patch.object(m.tempfile, 'TemporaryFile', side_effect=OSError('full')):
            self.assertEqual(self.request(path='%C3%A4%20file.txt')[0], 503)
        with patch.object(self.server.index, 'refresh', side_effect=sqlite_error()):
            self.assertEqual(self.request('HEAD', 'missing')[0], 503)


def sqlite_error():
    return m.sqlite3.DatabaseError('test')


if __name__ == '__main__':
    unittest.main()
