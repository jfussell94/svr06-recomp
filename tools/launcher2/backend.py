#!/usr/bin/env python3
"""SvR06 Launcher 2 - backend.

Reuses the game-domain logic of tools/launcher/server.py (builds, settings file, memory cards, library, cheats, logs) but
serves a new UI and never does slow work while a request is waiting: a background thread keeps a snapshot of everything the
UI shows, requests return that snapshot instantly.  Only the user's own actions (Play, Rebuild, browse dialogs) run inside a request.
"""
import json, mimetypes, os, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, unquote

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(TOOLS, 'launcher'))
sys.path.insert(0, HERE)
import server as sv            # noqa: E402  (old launcher domain logic)
import schema                  # noqa: E402

UI = os.path.join(HERE, 'ui')
ROOT = sv.ROOT
if getattr(sys, 'frozen', False) and os.path.isfile(os.path.join(ROOT, 'tools', 'launcher2', 'ui', 'app.js')): UI = os.path.join(ROOT, 'tools', 'launcher2', 'ui')   # packaged exe: live UI files win over the bundled copy
UI_PREFS = os.path.join(ROOT, 'config', 'launcher2_ui.json')
TOKEN = sv.TOKEN
LOCK = threading.RLock()
SNAP = {'light': {}, 'heavy': {}, 'ts': 0.0}
STOP = threading.Event()


# ------------------------------------------------------------- ui prefs
def ui_load():
    d = dict(schema.UI_DEFAULTS)
    try:
        with open(UI_PREFS, 'r', encoding='utf-8') as f: d.update(json.load(f))
    except Exception:
        pass
    return d


def ui_save(patch):
    d = ui_load()
    for k, v in patch.items():
        if k in schema.UI_DEFAULTS: d[k] = v
    os.makedirs(os.path.dirname(UI_PREFS), exist_ok=True)
    tmp = UI_PREFS + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f: json.dump(d, f, indent=1)
    os.replace(tmp, UI_PREFS)
    return d


# ------------------------------------------------------------- settings
def sel_id():
    return sv.selected_build()['id']


def values_for(bid):
    gs = sv.gs_read(bid)
    p = sv.load_prefs()
    out = {}
    for s in schema.S:
        k = s['key']
        if k == 'gpu': out[k] = p.get('gpu') or ''
        else: out[k] = gs.get(k, s['default'])
        if isinstance(out[k], float) and s['kind'] != 'gpu': out[k] = int(round(out[k]))
    return out


def clamp(s, v):
    if s['kind'] == 'gpu': return str(v or '')
    try: v = int(round(float(v)))
    except Exception: v = s['default']
    return max(s['min'], min(s['max'], v))


def set_values(changes):
    bid = sel_id()
    kv, prefs_changes = {}, {}
    restart = False
    for k, v in (changes or {}).items():
        s = schema.BY_KEY.get(k)
        if not s: continue
        v = clamp(s, v)
        if k == 'gpu': prefs_changes['gpu'] = v
        else: kv[k] = v
        if s['applies'] == 'restart': restart = True
    if prefs_changes:
        p = sv.load_prefs(); p.update(prefs_changes); sv.save_prefs(p)
    if kv:
        sv.gs_write_many(bid, kv)
        try: sv.gs_sync_all(bid)           # mirrors game.* options into the launcher prefs used for the game's environment
        except Exception: pass
    running = sv.game_running_cached()
    refresh_light(force=True)
    if restart and running: return {'ok': True, 'restart': True, 'message': 'Saved. Restart the game for this to take effect.'}
    return {'ok': True, 'restart': False, 'message': 'Saved' + (' and applied to the running game.' if running else '.')}


def apply_preset(pid):
    pr = next((x for x in schema.PRESETS if x['id'] == pid), None)
    if not pr: return {'ok': False, 'message': 'Unknown preset'}
    r = set_values(pr['values'])
    r['message'] = pr['name'] + ' applied.' + (' Restart the game for resolution changes.' if r.get('restart') else '')
    return r


def reset_page(page):
    ch = {s['key']: s['default'] for s in schema.S if s['page'] == page}
    return set_values(ch)


# ------------------------------------------------------------- cheats
def cheats_state():
    p = sv.load_prefs(); bid = sel_id()
    cur = sv.cheats_of(p, bid)
    return {'defs': [{k: c[k] for k in c if k != 'items'} | ({'items': [[a, b] for a, b in c['items']]} if 'items' in c else {}) for c in sv.CHEATS],
            'values': cur, 'verified': bool(sv.cheat_map(bid).get('verified') and sv.cheat_map(bid).get('shop_table')), 'msg': sv._CH['msg'],
            'choice': sv.CH_CHOICE}


def set_cheat(key, value):
    p = sv.load_prefs(); bid = sel_id()
    ch = dict(sv.cheats_of(p, bid)); ch[key] = value
    c = dict(p.get('cheats') or {}); c[bid] = ch; p['cheats'] = c
    sv.save_prefs(p)
    try: sv.gs_sync_all(bid)
    except Exception: pass
    refresh_light(force=True)
    return {'ok': True}


# ------------------------------------------------------------- snapshot
def refresh_light(force=False):
    try:
        hb = sv.heartbeat()
        light = {'running': sv.game_running_cached(), 'hb': hb, 'speed': sv.session_speed(hb), 'build': sv.build_state(),
                 'last_launched': sv.load_prefs().get('last_launched', '')}
        with LOCK:
            SNAP['light'] = light
        if force: refresh_heavy(sync=True)
    except Exception as e:
        with LOCK: SNAP['light'] = dict(SNAP['light'], err=str(e))


def refresh_heavy(sync=False):
    try:
        p = sv.load_prefs(); b = sv.selected_build(p); bid = b['id']
        try: sv.gs_sync_all(bid)
        except Exception: pass
        p = sv.load_prefs()
        ex = sv.exe_path()
        heavy = {
            'sel': bid, 'values': values_for(bid), 'builds': sv.builds_info(), 'cards': sv.cards_info(b), 'library': sv.library(),
            'cheats': cheats_state(), 'gpus': sv.gpu_list(), 'gpu_used': sv.gpu_used(), 'version': sv.current_version(),
            'icon_sets': [dict(s, ready=(not s['id']) or os.path.exists(os.path.join(ROOT, 'mods', 'icons', s['id'], 'glyph_idx.bin')))
                          for s in sv.ICON_SETS],
            'texpack': {'count': sv.count_files(sv.TEXPACK, '.png'), 'path': sv.TEXPACK_REL},
            'exe_mtime': os.path.getmtime(ex) if os.path.exists(ex) else 0, 'last_run': sv.last_run(),
            'scenarios': sorted(os.listdir(os.path.join(TOOLS, 'scenarios'))) if os.path.isdir(os.path.join(TOOLS, 'scenarios')) else [],
            'root': ROOT, 'stages': sv.STATES,
        }
        with LOCK: SNAP['heavy'] = heavy; SNAP['ts'] = time.time()
    except Exception as e:
        with LOCK: SNAP['heavy'] = dict(SNAP['heavy'], err=str(e))


def snapshot_loop():
    n = 0
    refresh_heavy()
    while not STOP.is_set():
        refresh_light()
        n += 1
        if n % 5 == 0: refresh_heavy()
        STOP.wait(1.5)


def snapshot():
    with LOCK:
        return {'now': time.time(), 'ui': ui_load(), 'light': SNAP['light'], 'heavy': SNAP['heavy']}


# ------------------------------------------------------------- slow data (cached, computed off the request thread)
_CACHE = {}


def cached(name, fn, ttl):
    e = _CACHE.get(name)
    if e and time.time() - e['t'] < ttl: return e['v']
    if e and e.get('busy'): return e['v']
    holder = _CACHE.setdefault(name, {'t': 0, 'v': None, 'busy': False})
    if holder['v'] is None:
        holder['v'] = fn(); holder['t'] = time.time(); return holder['v']
    holder['busy'] = True
    def bg():
        try: holder['v'] = fn(); holder['t'] = time.time()
        finally: holder['busy'] = False
    threading.Thread(target=bg, daemon=True).start()
    return holder['v']


# ------------------------------------------------------------- http
class H(BaseHTTPRequestHandler):
    server_version = 'SvR06Launcher/3'
    protocol_version = 'HTTP/1.1'

    def log_message(self, *a): pass

    def _send(self, code, body, ctype='application/json', extra=None):
        if isinstance(body, (dict, list)): body = json.dumps(body).encode('utf-8')
        elif isinstance(body, str): body = body.encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        for k, v in (extra or {}).items(): self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _authed(self, q):
        return self.headers.get('X-Token') == TOKEN or (q.get('t') or [''])[0] == TOKEN

    def do_GET(self):
        u = urlparse(self.path); q = parse_qs(u.query); path = unquote(u.path)
        try:
            if path in ('/', '/index.html'):
                html = open(os.path.join(UI, 'index.html'), 'r', encoding='utf-8').read().replace('__TOKEN__', TOKEN)
                return self._send(200, html, 'text/html; charset=utf-8')
            if path.startswith('/ui/'):
                fp = os.path.normpath(os.path.join(UI, path[4:]))
                if fp.startswith(UI) and os.path.isfile(fp):
                    return self._send(200, open(fp, 'rb').read(), mimetypes.guess_type(fp)[0] or 'application/octet-stream',
                                      {'Cache-Control': 'max-age=3600'} if '/fonts/' in path else None)
                return self._send(404, {'error': 'not found'})
            if path == '/favicon.ico': return self._send(204, b'', 'image/x-icon')
            if not self._authed(q): return self._send(403, {'error': 'bad token'})
            if path == '/api/schema':
                return self._send(200, {'settings': schema.S, 'presets': schema.PRESETS, 'pages': schema.PAGES, 'themes': schema.THEMES,
                                        'accents': schema.ACCENTS, 'hotkeys': schema.HOTKEYS, 'ui_defaults': schema.UI_DEFAULTS})
            if path == '/api/snapshot': return self._send(200, snapshot())
            if path == '/api/log':
                a = (q.get('src') or ['run_err'])[0]; f = (q.get('f') or [''])[0]; v = (q.get('ver') or [sel_id()])[0]
                return self._send(200, sv.read_log(a, f, v))
            if path == '/api/health': return self._send(200, cached('health', sv.health, 20))
            if path == '/api/shots': return self._send(200, sv.list_shots())
            if path == '/api/changelog': return self._send(200, cached('changelog', sv.changelog_entries, 60))
            if path == '/file':
                rel = (q.get('p') or [''])[0].replace('\\', '/')
                fp = os.path.normpath(os.path.join(ROOT, rel))
                ok = any(fp.startswith(os.path.normpath(os.path.join(ROOT, d)) + os.sep) for d in sv.SERVE_DIRS)
                if ok and os.path.isfile(fp) and fp.lower().endswith(('.png', '.jpg', '.jpeg', '.gif', '.webp', '.md', '.txt')):
                    return self._send(200, open(fp, 'rb').read(), mimetypes.guess_type(fp)[0] or 'application/octet-stream', {'Cache-Control': 'max-age=30'})
                return self._send(404, {'error': 'not found'})
            return self._send(404, {'error': 'unknown'})
        except Exception as e:
            return self._send(500, {'error': str(e)})

    def do_POST(self):
        u = urlparse(self.path); q = parse_qs(u.query)
        if not self._authed(q): return self._send(403, {'error': 'bad token'})
        try:
            n = int(self.headers.get('Content-Length') or 0)
            body = json.loads(self.rfile.read(n) or b'{}') if n else {}
            P = u.path
            if P == '/api/set': return self._send(200, set_values(body.get('changes') or {}))
            if P == '/api/preset': return self._send(200, apply_preset(body.get('id')))
            if P == '/api/reset': return self._send(200, reset_page(body.get('page')))
            if P == '/api/cheat': return self._send(200, set_cheat(body.get('key'), body.get('value')))
            if P == '/api/ui': return self._send(200, ui_save(body))
            if P == '/api/version':
                bid = body.get('id')
                if bid in sv.BUILD_BY_ID and not sv.BUILD_BY_ID[bid].get('planned'): sv.set_selected_build(bid)
                refresh_heavy(); refresh_light()
                return self._send(200, {'ok': True})
            if P == '/api/pref':          # a launcher pref that is not a game setting (card format etc.)
                p = sv.load_prefs()
                p.update({k: v for k, v in body.items() if k in ('psp_primary', 'card_format', 'card_file', 'gh_repo', 'logfilter')})
                sv.save_prefs(p); refresh_heavy()
                return self._send(200, {'ok': True})
            if P == '/api/action':
                msg = sv.do_action(body.get('name', ''), body.get('arg', ''), body)
                refresh_light(); threading.Thread(target=refresh_heavy, daemon=True).start()
                return self._send(200, {'message': msg})
            if P == '/api/chars': return self._send(200, sv.chars_op(body))
            if P == '/api/entrances': return self._send(200, sv.ent_op(body))
            if P == '/api/games':
                r = sv.games_op(body); threading.Thread(target=refresh_heavy, daemon=True).start()
                return self._send(200, r)
            if P == '/api/tickets': return self._send(200, sv.tickets_op(body))
            if P == '/api/exit':
                STOP.set()
                threading.Thread(target=lambda: (time.sleep(0.2), os._exit(0)), daemon=True).start()
                return self._send(200, {'ok': True})
            return self._send(404, {'error': 'unknown'})
        except Exception as e:
            return self._send(500, {'error': str(e)})


def make_server(port=0):
    threading.Thread(target=sv.cheat_loop, daemon=True).start()
    threading.Thread(target=snapshot_loop, daemon=True).start()
    return ThreadingHTTPServer(('127.0.0.1', port), H)
