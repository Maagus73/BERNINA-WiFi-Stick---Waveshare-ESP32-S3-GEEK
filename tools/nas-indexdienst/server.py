#!/usr/bin/env python3
"""BERNINA read-only index bridge v1. No source mutations; fail closed."""
import base64
import fnmatch
import hashlib
import hmac
import json
import re
import os
from pathlib import Path
import sqlite3
import stat
import tempfile
import threading
import time
import uuid
import unicodedata
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import quote, unquote, urlsplit
from xml.sax.saxutils import escape
from email.utils import formatdate


def handle_events(handler, index):
    from urllib.parse import parse_qs, urlsplit
    db = index.db
    db.execute('CREATE TABLE IF NOT EXISTS events(id INTEGER PRIMARY KEY, device TEXT, boot TEXT, seq INTEGER, ts INTEGER, received INTEGER, message TEXT, UNIQUE(device,boot,seq))')
    db.execute('CREATE INDEX IF NOT EXISTS events_time ON events(device,received,id)')
    def reply(status, value):
        handler.reply(status, json.dumps(value, ensure_ascii=False).encode(), {'Content-Type': 'application/json; charset=utf-8'})
    if handler.command == 'POST':
        try:
            body = json.loads(handler.request_body)
            device, boot, events = body['device'], body['boot'], body['events']
            if not re.fullmatch('[0-9a-f]{12}', device) or not re.fullmatch('[0-9a-f]{16}', boot) or not isinstance(events,list) or not 1 <= len(events) <= 16:
                raise ValueError()
            now = int(time.time())
            rows=[]
            for event in events:
                seq, ts, message = event['seq'], event['ts'], event['message']
                if type(seq) is not int or not 0 <= seq <= 0xffffffff or type(ts) is not int or not (ts == 0 or 1735689600 <= ts <= now+300) or not isinstance(message,str) or len(message.encode()) > 512 or '\x00' in message:
                    raise ValueError()
                rows.append((device,boot,seq,ts,now,message))
            with db:
                db.execute('DELETE FROM events WHERE received < ?', (now-3*86400,))
                needed=[r for r in rows if not db.execute('SELECT 1 FROM events WHERE device=? AND boot=? AND seq=?',r[:3]).fetchone()]
                count,size=db.execute('SELECT count(*),coalesce(sum(length(CAST(message AS BLOB))),0) FROM events WHERE device=? AND received>=?', (device,now-86400)).fetchone()
                if count+len(needed)>50000 or size+sum(len(r[-1].encode()) for r in needed)>10*1024*1024:
                    reply(507,{'error':'daily_event_limit'});return
                db.executemany('INSERT OR IGNORE INTO events(device,boot,seq,ts,received,message) VALUES(?,?,?,?,?,?)',rows)
            reply(200,{'accepted':len(rows)})
        except (ValueError,KeyError,TypeError,UnicodeError):
            reply(400,{'error':'invalid_events'})
        return
    if handler.command != 'GET':
        reply(405,{'error':'method'});return
    try:
        q=parse_qs(urlsplit(handler.path).query)
        device=q['device'][0];start=int(q['from'][0]);end=int(q['to'][0]);after=int(q.get('after',['0'])[0]);limit=int(q.get('limit',['100'])[0])
        if not re.fullmatch('[0-9a-f]{12}',device) or not 0<=after or not 1<=limit<=100 or not 0<end-start<=90000:
            raise ValueError()
        rows=db.execute('SELECT id,boot,seq,ts,received,message FROM events WHERE device=? AND ((ts>=? AND ts<?) OR (ts=0 AND received>=? AND received<?)) AND id>? ORDER BY id LIMIT ?', (device,start,end,start,end,after,limit+1)).fetchall()
        more=len(rows)>limit;rows=rows[:limit]
        reply(200,{'events':[dict(zip(('id','boot','seq','ts','received','message'),r)) for r in rows], 'next':rows[-1][0] if rows else after, 'more':more})
    except (ValueError,KeyError,TypeError):
        reply(400,{'error':'invalid_query'})


class Unready(Exception):
    pass


class Exclusions:
    """Root-anchored wildcards; ** as a complete component spans directory levels."""
    def __init__(self, text=''):
        if len(text.encode()) > 8192 or '\x00' in text:
            raise Unready('exclusion_config_invalid')
        self.rules = []
        for raw in text.split('\n'):
            rule = raw.strip(' \t\r')
            if not rule or rule.startswith('#'):
                continue
            if (not rule.startswith('/') or '//' in rule or len(rule.encode()) >= 128 or
                    any(ord(c) < 32 or ord(c) > 126 for c in rule) or
                    any(c in rule for c in ('[', ']', '\\', ':'))):
                raise Unready('exclusion_config_invalid')
            directory = rule.endswith('/')
            parts = rule.strip('/').split('/')
            if any(part in ('', '.', '..') for part in parts) or len(self.rules) == 32:
                raise Unready('exclusion_config_invalid')
            self.rules.append(([p.upper() for p in parts], directory))
        self.text = text
        self.digest = hashlib.sha256(text.encode()).hexdigest()

    def matches(self, path, directory):
        # ASCII patterns/case fold are identical to the bounded ESP matcher.
        parts = [p.translate(str.maketrans('abcdefghijklmnopqrstuvwxyz', 'ABCDEFGHIJKLMNOPQRSTUVWXYZ')) for p in path.split('/')]
        for pattern, subtree in self.rules:
            states = {0}
            def closure(states):
                for i in range(len(pattern)):
                    if i in states and pattern[i] == '**':
                        states.add(i + 1)
            closure(states)
            for position, part in enumerate(parts):
                next_states = set()
                for i in states:
                    if i == len(pattern):
                        continue
                    if pattern[i] == '**':
                        next_states.add(i)
                    elif fnmatch.fnmatchcase(part, pattern[i]):
                        next_states.add(i + 1)
                closure(next_states)
                states = next_states
                if len(pattern) in states and (subtree and (position < len(parts) - 1 or directory)
                                               or not subtree and position == len(parts) - 1 and not directory):
                    return True
        return False


def signature(s):
    return [s.st_dev, s.st_ino, s.st_mode, s.st_size, s.st_mtime_ns, s.st_ctime_ns]


class Index:
    def __init__(self, source, state, nas_rules=None, sd_rules=None):
        self.source = Path(source)
        self.state = Path(state)
        self.state.mkdir(parents=True, exist_ok=True)
        if self.source.resolve() == self.state.resolve() or self.source.resolve() in self.state.resolve().parents:
            raise Unready('state_inside_source')
        self.db = sqlite3.connect(self.state / 'index.sqlite', check_same_thread=False)
        self.db.execute('PRAGMA synchronous=FULL')
        self.db.execute('CREATE TABLE IF NOT EXISTS files(path TEXT PRIMARY KEY, sig TEXT, hash TEXT, checked REAL)')
        self.db.execute('CREATE TABLE IF NOT EXISTS identity(value TEXT)')
        self.db.execute('CREATE TABLE IF NOT EXISTS root(value TEXT)')
        row = self.db.execute('SELECT value FROM identity').fetchone()
        self.identity = row[0] if row else uuid.uuid4().hex
        if not row:
            self.db.execute('INSERT INTO identity VALUES(?)', (self.identity,))
            self.db.commit()
        self.lock = threading.RLock()
        self.rows = {}
        self.token = ''
        self.initial = True
        self.rule_paths = (Path(nas_rules) if nas_rules else None, Path(sd_rules) if sd_rules else None)
        self.nas_rules = Exclusions()
        self.sd_rules = Exclusions()
        self.rule_signatures = None
        self.source_id = ''
        self.refreshes = 0

    def rule_stats(self):
        return tuple(signature(p.stat()) if p else None for p in self.rule_paths)

    def read_stable_rules(self):
        # Bound both reads and retries; do not acknowledge metadata from an
        # unstable read or parse a replacement under the old file identity.
        for _ in range(3):
            before = self.rule_stats()
            policies = []
            stable = True
            for i, path in enumerate(self.rule_paths):
                if path is None:
                    policies.append(Exclusions())
                    continue
                with path.open('rb') as stream:
                    if signature(os.fstat(stream.fileno())) != before[i]:
                        stable = False
                        break
                    raw = stream.read(8193)
                    if signature(os.fstat(stream.fileno())) != before[i]:
                        stable = False
                        break
                if len(raw) > 8192:
                    raise Unready('exclusion_config_invalid')
                try:
                    # Keep read_text's historical universal-newline semantics,
                    # so existing policy digests/checkpoints stay compatible.
                    text = raw.decode('utf-8').replace('\r\n', '\n').replace('\r', '\n')
                except UnicodeDecodeError:
                    raise Unready('exclusion_config_invalid') from None
                policies.append(Exclusions(text))
            if stable and before == self.rule_stats():
                return before, policies
        print(json.dumps({'event': 'rules_read_unstable', 'code': 'exclusion_config_changed'}), flush=True)
        raise Unready('exclusion_config_changed')

    def load_rules(self):
        if self.rule_stats() != self.rule_signatures:
            signatures, policies = self.read_stable_rules()
            self.nas_rules, self.sd_rules = policies
            self.rule_signatures = signatures

    def check_current_rules(self):
        current = self.rule_stats()
        if current == self.rule_signatures:
            return
        signatures, policies = self.read_stable_rules()
        old_policies = (self.nas_rules, self.sd_rules)
        changed = False
        names = ('device', 'inode', 'mode', 'size', 'mtime', 'ctime')
        for i, label in enumerate(('nas', 'sd')):
            content_changed = policies[i].digest != old_policies[i].digest
            if signatures[i] != self.rule_signatures[i] or content_changed:
                old, new = self.rule_signatures[i], signatures[i]
                fields = [name for j, name in enumerate(names) if old is None or new is None or old[j] != new[j]]
                print(json.dumps({'event': 'rules_content_changed' if content_changed else 'rules_metadata_only',
                                  'rules': label, 'metadata_fields': fields}), flush=True)
            changed |= content_changed
        if changed:
            # Keep the old policy, signatures and plan; only a fresh complete
            # NAS refresh may install changed rules and a new generation.
            raise Unready('exclusion_config_changed')
        self.rule_signatures = signatures

    def resource_ready(self):
        if self.initial or not self.token or not self.source_id:
            raise Unready('snapshot_missing')
        root = json.dumps(signature(self.source.lstat())[:2])
        if self.db.execute('SELECT value FROM root').fetchone()[0] != root:
            raise Unready('source_root_changed')
        self.check_current_rules()

    def path_absent(self, path):
        # lstat through no-follow directory descriptors, including absent parents.
        fd = os.open(self.source, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            parts = path.split('/')
            for part in parts[:-1]:
                try:
                    nxt = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
                except FileNotFoundError:
                    return True
                os.close(fd)
                fd = nxt
            try:
                os.stat(parts[-1], dir_fd=fd, follow_symlinks=False)
                return False
            except FileNotFoundError:
                return True
        finally:
            os.close(fd)


    def enumerate(self):
        if self.source.is_symlink() or not self.source.is_dir():
            raise Unready('source_missing')
        result = {}
        pending = [self.source]
        while pending:
            directory = pending.pop()
            s = directory.lstat()
            if not stat.S_ISDIR(s.st_mode):
                raise Unready('directory_changed')
            for p in directory.iterdir():
                rel = p.relative_to(self.source).as_posix()
                if rel in ('.bernina-index-v1', '.bernina-sd-excludes-v1', '.bernina-nas-excludes-v1', '.bernina-plan-v2', '.bernina-bundle-v1', '.bernina-events-v1') or len(rel.encode('utf-8')) >= 1024 or any(ord(c) < 32 or ord(c) == 127 for c in rel):
                    raise Unready('unsupported_name')
                v = p.lstat()
                if rel.split('/')[0].upper() == 'BNCACHE':
                    raise Unready('reserved_source_name')
                if self.nas_rules.matches(rel, stat.S_ISDIR(v.st_mode)):
                    continue
                if stat.S_ISLNK(v.st_mode) or not (stat.S_ISDIR(v.st_mode) or stat.S_ISREG(v.st_mode)):
                    raise Unready('special_file_or_symlink')
                result[rel] = signature(v)
                if stat.S_ISDIR(v.st_mode):
                    pending.append(p)
            if signature(directory.lstat()) != signature(s):
                raise Unready('directory_changed')
        return result

    def open_source(self, path):
        # Resolve every component relative to directory descriptors. Never follow symlinks.
        fd = os.open(self.source, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        try:
            parts = path.split('/')
            for part in parts[:-1]:
                nxt = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=fd)
                os.close(fd)
                fd = nxt
            result = os.open(parts[-1], os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=fd)
            if not stat.S_ISREG(os.fstat(result).st_mode):
                os.close(result)
                raise Unready('not_regular')
            return os.fdopen(result, 'rb')
        finally:
            os.close(fd)

    def digest(self, path, expected, output=None):
        digest = hashlib.sha256()
        with self.open_source(path) as f:
            if signature(os.fstat(f.fileno())) != expected:
                raise Unready('file_changed')
            while True:
                block = f.read(1024 * 1024)
                if not block:
                    break
                digest.update(block)
                if output is not None:
                    if output.write(block) != len(block):
                        raise Unready('spool_short_write')
            if signature(os.fstat(f.fileno())) != expected:
                raise Unready('file_changed')
        return digest.hexdigest()

    def refresh(self):
        with self.lock:
            self.refreshes += 1
            self.load_rules()
            # Full authority boundaries enumerate twice; resource requests verify their file.
            root_id = json.dumps(signature(self.source.lstat())[:2])
            root_row = self.db.execute('SELECT value FROM root').fetchone()
            if root_row and root_row[0] != root_id:
                raise Unready('source_root_changed')
            before = self.enumerate()
            rows = {}
            now = time.time()
            updates = []
            cached_rows = {p: (sig, digest, checked) for p, sig, digest, checked in self.db.execute('SELECT path,sig,hash,checked FROM files')}
            for path, sig in sorted(before.items()):
                directory = stat.S_ISDIR(sig[2])
                digest = '-'
                if not directory:
                    cached = cached_rows.get(path)
                    if not self.initial and cached and json.loads(cached[0]) == sig and 0 <= now - cached[2] < 3600:
                        digest = cached[1]
                    else:
                        digest = self.digest(path, sig)
                        updates.append((path, json.dumps(sig), digest, now))
                rows[path] = ('D' if directory else 'F', 0 if directory else sig[3], digest, sig)
            if before != self.enumerate() or root_id != json.dumps(signature(self.source.lstat())[:2]):
                raise Unready('source_changed_during_scan')
            # Commit only a fully checked generation; failed scans cannot publish absence.
            removed = set(cached_rows) - {p for p, row in rows.items() if row[0] == 'F'}
            if updates or removed or not root_row:
                with self.db:
                    if not root_row:
                        self.db.execute('INSERT INTO root VALUES(?)', (root_id,))
                    self.db.executemany('INSERT OR REPLACE INTO files VALUES(?,?,?,?)', updates)
                    self.db.executemany('DELETE FROM files WHERE path=?', ((p,) for p in removed))
            self.rows = rows
            encoded = self.records()
            self.source_id = hashlib.sha256((self.identity + root_id).encode()).hexdigest()
            self.token = hashlib.sha256((self.source_id + self.nas_rules.digest + self.sd_rules.digest).encode() + encoded).hexdigest()
            self.initial = False
            return self.token

    def records(self):
        return ''.join(f'{kind}\t{size}\t{digest}\t{quote(path, safe="/")}\n'
                       for path, (kind, size, digest, _) in sorted(self.rows.items())).encode('ascii')

    def plan(self, base, threshold, force=False):
        """Immutable NAS-side plan. Publishing a snapshot NEVER acknowledges an ESP."""
        if not 1 <= threshold <= 20000:
            raise Unready('plan_configuration')
        policy = self.nas_rules.digest + ':' + self.sd_rules.digest
        current = {p: list(v[:3]) for p, v in self.rows.items()}
        keys = set()
        for path, row in current.items():
            parts = path.split('/')
            if any(len(part.encode()) > 255 or part.endswith((' ', '.')) or
                   any(c in '<>:"\\|?*' or ord(c) < 32 for c in part) for part in parts):
                raise Unready('plan_path_conflict')
            key = unicodedata.normalize('NFC', path).upper()
            if key in keys or parts[0].upper().startswith(('.B16-', '.B19-', '.B20-')):
                raise Unready('plan_path_conflict')
            keys.add(key)
            parent = path.rpartition('/')[0]
            if parent and (parent not in current or current[parent][0] != 'D'):
                raise Unready('plan_path_conflict')
            if self.sd_rules.matches(path, row[0] == 'D'):
                raise Unready('plan_exclusion_conflict')
        self.db.execute('CREATE TABLE IF NOT EXISTS snapshots(token TEXT PRIMARY KEY, source TEXT, policy TEXT, rows TEXT, created REAL)')
        self.db.execute('CREATE TABLE IF NOT EXISTS plans(id TEXT PRIMARY KEY, token TEXT, source TEXT, policy TEXT, body BLOB, created REAL)')
        previous = self.db.execute('SELECT source,policy,rows FROM snapshots WHERE token=?', (base,)).fetchone()
        known = not force and previous and previous[:2] == (self.source_id, policy)
        old = json.loads(previous[2]) if known else {}
        changes = sum(1 for p in current.keys() | old.keys()
                      if current.get(p) != old.get(p) and
                      ((current.get(p) or [''])[0] == 'F' or (old.get(p) or [''])[0] == 'F'))
        full = not known or changes > threshold
        records = []
        if not full:
            for path, row in sorted(old.items(), key=lambda item: (-item[0].count('/'), item[0]), reverse=False):
                if path not in current or current[path][0] != row[0]:
                    if self.nas_rules.matches(path, row[0] == 'D') or self.sd_rules.matches(path, row[0] == 'D'):
                        continue
                    records.append(('R' if row[0] == 'D' else 'X', 0, '-' if row[0] == 'D' else '0'*64, path))
        for path, row in sorted(current.items(), key=lambda item: (item[0].count('/'), item[0])):
            if row[0] == 'D':
                records.append(('D' if full or old.get(path) != row else 'K', 0, '-', path))
        for path, row in sorted(current.items()):
            if row[0] == 'F':
                records.append(('F' if full or old.get(path) != row else 'V', row[1], row[2], path))
        files = sum(1 for r in records if r[0] == 'F')
        size = sum(r[1] for r in records if r[0] == 'F')
        mode = 'FULL' if full else 'DELTA'
        body = (f'BPLAN2\t{self.token}\t{mode}\t{files}\t{size}\t{len(records)}\t{changes}\n' +
                ''.join(f'{kind}\t{length}\t{digest}\t{quote(path, safe="/")}\n' for kind,length,digest,path in records) +
                f'END\t{len(records)}\t{self.token}\n').encode()
        plan_id = hashlib.sha256(body).hexdigest()
        with self.db:
            self.db.execute('INSERT OR IGNORE INTO snapshots VALUES(?,?,?,?,?)',
                            (self.token, self.source_id, policy, json.dumps(current, ensure_ascii=False), time.time()))
            self.db.execute('INSERT OR IGNORE INTO plans VALUES(?,?,?,?,?,?)',
                            (plan_id, self.token, self.source_id, policy, body, time.time()))
            self.db.execute('DELETE FROM snapshots WHERE token NOT IN (SELECT token FROM snapshots ORDER BY created DESC LIMIT 32)')
            self.db.execute('DELETE FROM plans WHERE id NOT IN (SELECT id FROM plans ORDER BY created DESC LIMIT 32)')
        return plan_id, body

    def saved_plan(self, plan_id):
        if len(plan_id) != 64 or any(c not in '0123456789abcdef' for c in plan_id):
            raise Unready('plan_missing')
        try:
            row = self.db.execute('SELECT token,source,policy,body FROM plans WHERE id=?', (plan_id,)).fetchone()
        except sqlite3.OperationalError:
            row = None
        if not row or row[:3] != (self.token, self.source_id, self.nas_rules.digest + ':' + self.sd_rules.digest):
            raise Unready('plan_missing')
        if hashlib.sha256(row[3]).hexdigest() != plan_id:
            raise Unready('plan_corrupt')
        return row[3]

    def manifest(self):
        data = self.records()
        return b'BINDEX1\t' + self.token.encode() + b'\n' + data + f'END\t{len(self.rows)}\t{self.token}\n'.encode()


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        self.connection.settimeout(30)

    def log_message(self, *args):
        pass  # Do not log credentials or private paths.

    def reply(self, status, data=b'', headers=None):
        self.send_response(status)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        if self.command != 'HEAD':
            self.wfile.write(data)

    def dispatch(self):
        expected = 'Basic ' + base64.b64encode((self.server.username + ':' + self.server.password).encode()).decode()
        if not hmac.compare_digest(self.headers.get('Authorization', ''), expected):
            self.reply(401, headers={'WWW-Authenticate': 'Basic realm="BERNINA"'})
            return
        raw = urlsplit(self.path).path
        prefix = self.server.prefix
        if not raw.startswith(prefix):
            self.reply(404)
            return
        path = unquote(raw[len(prefix):], errors='strict').rstrip('/')
        if any(p in ('.', '..', '') for p in path.split('/')) and path:
            self.reply(400)
            return
        if '\\' in path or '\x00' in path:
            self.reply(400)
            return
        idx = self.server.index
        with idx.lock:
            if path == '.bernina-events-v1':
                handle_events(self, idx)
                return
            pinned = self.headers.get('X-Bernina-Generation')
            resource_check = self.headers.get('X-Bernina-Resource-Check') == '1' and self.command in ('GET', 'HEAD') and path not in ('.bernina-plan-v2', '.bernina-index-v1', '.bernina-sd-excludes-v1', '.bernina-nas-excludes-v1')
            if resource_check:
                if not pinned:
                    self.reply(428)
                    return
                idx.resource_ready()
                token = idx.token
            else:
                token = idx.refresh()
            if pinned and pinned != token:
                self.reply(409)
                return
            metadata = {'X-Bernina-Generation': token, 'X-Bernina-Source': idx.source_id,
                        'X-Bernina-Policy': idx.nas_rules.digest + ':' + idx.sd_rules.digest, 'X-Bernina-Bundle': '1', 'X-Bernina-Files': str(sum(r[0]=='F' for r in idx.rows.values())), 'X-Bernina-Directories': str(sum(r[0]=='D' for r in idx.rows.values()))}
            if path in ('.bernina-sd-excludes-v1', '.bernina-nas-excludes-v1') and self.command == 'GET':
                rules = idx.sd_rules if path == '.bernina-sd-excludes-v1' else idx.nas_rules
                self.reply(200, rules.text.encode(), metadata)
                return
            if path == '.bernina-index-v1' and self.command == 'GET':
                self.reply(200, idx.manifest(), {**metadata, 'Content-Type': 'text/plain'})
                return
            if path == '.bernina-plan-v2' and self.command == 'GET':
                value = self.headers.get('X-Bernina-Threshold', '25')
                if not value.isdecimal() or not 1 <= int(value) <= 20000:
                    self.reply(400)
                    return
                plan_id, body = idx.plan(self.headers.get('X-Bernina-Base', ''), int(value), self.headers.get('X-Bernina-Full') == '1')
                self.reply(200, body, {**metadata, 'X-Bernina-Plan': plan_id})
                return
            if path.startswith('.bernina-plan-v2/') and self.command == 'GET':
                plan_id = path.split('/')[1]
                body = idx.saved_plan(plan_id)
                parts = path.split('/')
                extra = {}
                if len(parts) == 3:
                    if not parts[2].isdecimal() or int(parts[2]) >= len(body):
                        self.reply(416)
                        return
                    offset = int(parts[2])
                    chunk = body[offset:offset+65536]
                    extra = {'X-Bernina-Part': hashlib.sha256(chunk).hexdigest(), 'X-Bernina-Total': str(len(body)), 'X-Bernina-Offset': str(offset)}
                    body = chunk
                elif len(parts) != 2:
                    self.reply(400)
                    return
                self.reply(200, body, {**metadata, 'X-Bernina-Plan': plan_id, **extra})
                return
            if path.startswith('.bernina-bundle-v1/') and self.command == 'GET':
                parts = path.split('/')
                if len(parts) != 4 or not parts[2].isdecimal() or not parts[3].isdecimal():
                    self.reply(400)
                    return
                self.stream_bundle(idx, parts[1], int(parts[2]), int(parts[3]), metadata)
                return
            row = idx.rows.get(path)
            if self.command == 'PROPFIND':
                depth = self.headers.get('Depth', '1')
                if depth not in ('0', '1'):
                    self.reply(400)
                    return
                if path and (not row or row[0] != 'D'):
                    self.reply(404)
                    return
                paths = [path]
                if depth == '1':
                    paths += [p for p in idx.rows if p.rpartition('/')[0] == path]
                parts = ['<?xml version="1.0" encoding="utf-8"?><d:multistatus xmlns:d="DAV:">']
                for p in paths:
                    kind, size, digest, sig = idx.rows[p] if p else ('D', 0, '-', None)
                    href = escape(prefix + quote(p, safe='/') + ('/' if kind == 'D' and p else ''))
                    resource = '<d:collection/>' if kind == 'D' else ''
                    props = f'<d:resourcetype>{resource}</d:resourcetype>'
                    if kind == 'F':
                        props += f'<d:getcontentlength>{size}</d:getcontentlength><d:getetag>"{digest}"</d:getetag>'
                    parts.append(f'<d:response><d:href>{href}</d:href><d:propstat><d:prop>{props}</d:prop><d:status>HTTP/1.1 200 OK</d:status></d:propstat></d:response>')
                parts.append('</d:multistatus>')
                self.reply(207, ''.join(parts).encode(), {'Content-Type': 'application/xml; charset=utf-8'})
                return
            if self.command not in ('GET', 'HEAD'):
                self.reply(405)
                return
            if path and not row:
                if resource_check and not idx.path_absent(path):
                    self.reply(409)
                    return
                self.reply(404)
                return
            if not row or row[0] != 'F':
                self.reply(405)
                return
            kind, size, digest, sig = row
            etag = '"' + digest + '"'
            if self.headers.get('If-Match') not in (None, etag):
                self.reply(412)
                return
            # Verify the actual bytes before sending any success headers, including HEAD.
            with tempfile.TemporaryFile(dir=idx.state) as spool:
                actual = idx.digest(path, sig, spool if self.command == 'GET' else None)
                if actual != digest:
                    self.reply(409)
                    return
                if self.command == 'GET':
                    spool.flush()
                    if os.fstat(spool.fileno()).st_size != size:
                        raise Unready('spool_length')
                    spool.seek(0)
                    stored = hashlib.sha256()
                    while block := spool.read(1024 * 1024):
                        stored.update(block)
                    if stored.hexdigest() != digest:
                        raise Unready('spool_checksum')
                if resource_check:
                    idx.resource_ready()
                if actual != digest or (not resource_check and idx.refresh() != token):
                    self.reply(409)
                    return
                self.send_response(200)
                self.send_header('Content-Length', str(size))
                self.send_header('ETag', etag)
                self.send_header('Digest', 'sha-256=' + base64.b64encode(bytes.fromhex(digest)).decode())
                self.send_header('Last-Modified', formatdate(sig[4] / 1e9, usegmt=True))
                self.send_header('Cache-Control', 'no-store')
                self.end_headers()
                if self.command == 'GET':
                    spool.seek(0)
                    while block := spool.read(65536):
                        self.wfile.write(block)

    def stream_bundle(self, idx, plan_id, start, count, metadata):
        """Single-pass source reads; no per-file HTTP requests or NAS spooling."""
        if not 1 <= count <= 128 or start < 0:
            self.reply(400)
            return
        plan = idx.saved_plan(plan_id)
        catalog = []
        for raw in plan.decode('ascii').splitlines()[1:-1]:
            parts = raw.split('\t')
            if parts[0] == 'F':
                catalog.append((unquote(parts[3], errors='strict'), int(parts[1]), parts[2], parts[3]))
        selected = catalog[start:start+count]
        if len(selected) != count:
            self.reply(416)
            return
        frames = []
        total = 0
        for path, size, digest, encoded in selected:
            row = idx.rows.get(path)
            if not row or row[:3] != ('F', size, digest):
                raise Unready('file_changed')
            frame = f'FILE\t{size}\t{digest}\t{encoded}\n'.encode('ascii')
            frames.append((path, size, digest, row[3], frame))
            total += len(frame) + size + 1
        head = f'BUNDLE1\t{plan_id}\t{start}\t{count}\n'.encode()
        end = f'END\t{plan_id}\t{count}\n'.encode()
        total += len(head) + len(end)
        transfer = uuid.uuid4().hex
        began = time.monotonic()
        last_progress = began
        wire_bytes = source_bytes = completed = 0
        def record(event, **fields):
            print(json.dumps({'event': event, 'transfer': transfer, 'plan': plan_id,
                              'first': start, 'count': count, 'wire_bytes': wire_bytes,
                              'expected_wire': total, 'source_bytes': source_bytes,
                              'completed_files': completed,
                              'elapsed_ms': round((time.monotonic()-began)*1000), **fields}), flush=True)
        def send(data):
            nonlocal wire_bytes, last_progress
            self.wfile.write(data)
            wire_bytes += len(data)
            now = time.monotonic()
            if now - last_progress >= 10:
                record('bundle_progress')
                last_progress = now
        record('bundle_start')
        self.close_connection = True
        try:
            self.send_response(200)
            self.send_header('Connection', 'close')
            self.send_header('Content-Length', str(total))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('X-Bernina-Transfer', transfer)
            for key, value in {**metadata, 'X-Bernina-Plan': plan_id}.items():
                self.send_header(key, value)
            self.end_headers()
            send(head)
            for path, size, digest, sig, frame in frames:
                with idx.open_source(path) as source:
                    current_sig = signature(os.fstat(source.fileno()))
                    if current_sig != sig:
                        # Verify the immutable plan hash before allowing metadata drift.
                        # Keep this descriptor open; never substitute a newer version.
                        if current_sig[3] != size:
                            raise Unready('file_changed')
                        check = hashlib.sha256()
                        while block := source.read(65536):
                            check.update(block)
                        if signature(os.fstat(source.fileno())) != current_sig or check.hexdigest() != digest:
                            raise Unready('file_changed')
                        source.seek(0)
                        sig = current_sig
                        record('file_metadata_verified', file_ordinal=start+completed)
                    send(frame)
                    hashed = hashlib.sha256()
                    received = 0
                    while received < size:
                        block = source.read(min(65536, size-received))
                        if not block:
                            raise Unready('file_changed')
                        hashed.update(block)
                        received += len(block)
                        source_bytes += len(block)
                        send(block)
                    if source.read(1) or signature(os.fstat(source.fileno())) != sig or hashed.hexdigest() != digest:
                        raise Unready('file_changed')
                send(b'\n')
                completed += 1
            idx.resource_ready()
            send(end)
            record('bundle_complete')
        except (OSError, Unready) as error:
            # Local socket acceptance is not proof that the ESP received the bytes.
            self.close_connection = True
            code = ('file_changed' if isinstance(error, Unready) and error.args == ('file_changed',)
                    else 'source_state_changed' if isinstance(error, Unready)
                    else 'source_or_transport_io')
            record('bundle_aborted', code=code, errno=getattr(error, 'errno', None))
        except Exception:
            # Headers may already be sent: never append a second HTTP response.
            self.close_connection = True
            record('bundle_aborted', code='unexpected_stream_error', errno=None)

    def handle_request(self):
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if length < 0 or length > 8192 or self.headers.get('Transfer-Encoding'):
                self.close_connection = True
                self.reply(400)
                return
            self.request_body = self.rfile.read(length)
            if len(self.request_body) != length:
                self.close_connection = True
                self.reply(400)
                return
            self.dispatch()
        except (BrokenPipeError, ConnectionResetError):
            self.close_connection = True
        except Exception as error:
            # Fixed codes only: never expose exception text, paths or credentials.
            known = {'state_inside_source', 'source_missing', 'directory_changed',
                     'unsupported_name', 'special_file_or_symlink', 'not_regular',
                     'file_changed', 'spool_short_write', 'source_root_changed',
                     'source_changed_during_scan', 'spool_length', 'spool_checksum',
                     'exclusion_config_invalid', 'exclusion_config_changed', 'snapshot_missing', 'reserved_source_name', 'plan_configuration', 'plan_path_conflict', 'plan_exclusion_conflict', 'plan_missing', 'plan_corrupt'}
            code = 'index_error'
            if isinstance(error, Unready) and error.args and isinstance(error.args[0], str) and error.args[0] in known:
                code = error.args[0]
            elif isinstance(error, sqlite3.Error):
                code = 'database_error'
            elif isinstance(error, PermissionError):
                code = 'permission_denied'
            elif isinstance(error, FileNotFoundError):
                code = 'path_missing'
            elif isinstance(error, OSError):
                code = 'filesystem_error'
            record = {'event': 'request_failed', 'status': 503, 'code': code}
            if isinstance(error, OSError) and isinstance(error.errno, int):
                record['errno'] = error.errno
            print(json.dumps(record), flush=True)
            self.close_connection = True
            self.reply(503, headers={'X-Bernina-Error': code})

    do_GET = do_HEAD = do_PROPFIND = do_POST = handle_request


class IndexHTTPServer(ThreadingHTTPServer):
    """Bound idle keep-alive workers as well as active requests on the NAS."""
    daemon_threads = True

    def __init__(self, address, handler):
        self.workers = threading.BoundedSemaphore(8)
        super().__init__(address, handler)

    def process_request(self, request, address):
        if not self.workers.acquire(blocking=False):
            try:
                request.sendall(b'HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\nConnection: close\r\nX-Bernina-Error: server_busy\r\n\r\n')
            finally:
                self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except Exception:
            self.workers.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.workers.release()


def make_server(source, state, username, password, prefix='/', address=('127.0.0.1', 8080), nas_rules=None, sd_rules=None):
    if not username or not password or ':' in username or not prefix.startswith('/') or not prefix.endswith('/'):
        raise ValueError('invalid_configuration')
    server = IndexHTTPServer(address, Handler)
    server.timeout = 1
    server.index = Index(source, state, nas_rules, sd_rules)
    server.username, server.password, server.prefix = username, password, prefix
    return server


if __name__ == '__main__':
    credentials = json.loads(Path('/run/credentials.json').read_text())
    server = make_server('/source', '/state', credentials['username'], credentials['password'],
                         os.environ.get('URL_PREFIX', '/'), ('0.0.0.0', 8080),
                         os.environ.get('NAS_EXCLUDES'), os.environ.get('SD_EXCLUDES'))
    print(json.dumps({'service': 'bernina-index-v1', 'version': '1.0.0', 'events': 'v1', 'planner': 'v2', 'bundle': 'v1', 'diagnostics': 'v1', 'rules_check': 'content-v2', 'file_check': 'hash-v2', 'source_read_only': True}), flush=True)
    server.serve_forever()
