#!/usr/bin/env python3
"""SvR06 Launcher (web edition) - backend.

A tiny dependency-free HTTP server bound to 127.0.0.1 that powers the launcher UI in ui/.
Every API call needs the per-session token that is injected into index.html, so other web pages
cannot talk to it. Files are only served from a fixed set of project sub folders.
"""
import glob, json, mimetypes, os, re, secrets, subprocess, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs, unquote

if getattr(sys, 'frozen', False):                   # packaged SvR06Launcher.exe: UI is bundled, project folders sit next to / above the exe
    HERE = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(sys.executable)))
    UI = os.path.join(HERE, 'ui')
    _d = os.path.dirname(os.path.abspath(sys.executable))
    _r = None
    for _c in (_d, os.path.dirname(_d)):
        if os.path.isdir(os.path.join(_c, 'scripts')) and os.path.isdir(os.path.join(_c, 'tools')): _r = _c; break
    ROOT = os.path.abspath(os.environ.get('SVR06_LAUNCHER_ROOT') or _r or _d)
    TOOLS = os.path.join(ROOT, 'tools')
else:
    HERE = os.path.dirname(os.path.abspath(__file__))
    UI = os.path.join(HERE, 'ui')
    TOOLS = os.path.dirname(HERE)
    ROOT = os.path.abspath(os.environ.get('SVR06_LAUNCHER_ROOT') or os.path.dirname(TOOLS))
sys.path.insert(0, TOOLS)
try:
    import svrlive
    LIVE = svrlive.LIVE
except Exception:                                   # keeps the UI usable (and testable) without the live console module
    svrlive = None
    LIVE = os.path.join(ROOT, 'analysis', 'live')

IS_WIN = os.name == 'nt'
NOWIN = 0x08000000 if IS_WIN else 0
PREFS = os.path.join(ROOT, 'config', 'launcher_prefs.json')
TOKEN = secrets.token_urlsafe(18)
START = time.time()
LAST_PING = [0.0]

MODES = [
    {'id': 'normal', 'name': 'Normal', 'desc': 'VU1 speed-ups on (recommended)', 'flags': 0},
    {'id': 'safe', 'name': 'Safe', 'desc': 'VU1 speed-ups off (CL-0133 off), slower but the reference path', 'flags': 1 << 29},
]
ICON_SETS = [
    {'id': '', 'name': 'Original (PS2)', 'sub': 'The game\'s own icons'},
    {'id': 'xboxone', 'name': 'Xbox One', 'sub': 'A B X Y, clean modern'},
    {'id': 'xbox360', 'name': 'Xbox 360', 'sub': 'Retro glossy'},
    {'id': 'ps3', 'name': 'PS3', 'sub': 'Retro PlayStation'},
    {'id': 'ps4', 'name': 'PS4', 'sub': 'DualShock 4'},
    {'id': 'ps5', 'name': 'PS5', 'sub': 'DualSense'},
    {'id': 'switchpro', 'name': 'Switch Pro', 'sub': 'Nintendo layout'},
    {'id': 'switchpro2', 'name': 'Switch Pro 2', 'sub': 'Alternate style'},
    {'id': 'mypack', 'name': 'Custom pack', 'sub': 'mods/icons/mypack'},
]
DEFAULT_PREFS = {'mode': 'normal', 'icons': '', 'texpack': False, 'probes': True, 'skipmovies': False,
                 'accent': '#e11d48', 'theme': 'dark', 'autoquit': True, 'logfilter': '', 'page': 'home',
                 'games': {}, 'psp_primary': '', 'arcade': False, 'cheats': {}, 'build': '', 'last_launched': ''}
TEXPACK_REL = os.path.join('mods', 'texturepack', 'emulation_lovers_hd', 'replacements')
TEXPACK = os.path.join(ROOT, TEXPACK_REL)
SERVE_DIRS = ['mods', os.path.join('analysis', 'shots'), os.path.join('analysis', 'live', 'shots'), 'docs']


# ---------------------------------------------------------------- helpers
def load_prefs():
    p = dict(DEFAULT_PREFS)
    try:
        with open(PREFS, 'r', encoding='utf-8') as f:
            raw = json.load(f)
        # migrate the Tk launcher's prefs (mode/icons were display names there)
        names = {s['name']: s['id'] for s in ICON_SETS}
        names['Custom (mods/icons/mypack)'] = 'mypack'
        if raw.get('icons') in names: raw['icons'] = names[raw['icons']]
        if raw.get('mode') and raw['mode'] not in [m['id'] for m in MODES]:
            raw['mode'] = 'safe' if 'Safe' in str(raw['mode']) else 'normal'
        for k in DEFAULT_PREFS:
            if k in raw: p[k] = raw[k]
    except Exception:
        pass
    return p


def save_prefs(p):
    os.makedirs(os.path.dirname(PREFS), exist_ok=True)
    clean = {k: p.get(k, DEFAULT_PREFS[k]) for k in DEFAULT_PREFS}
    with open(PREFS, 'w', encoding='utf-8') as f:
        json.dump(clean, f, indent=1)
    return clean


def build_env(p):
    e = dict(os.environ)
    mode = next((m for m in MODES if m['id'] == p.get('mode')), MODES[0])
    if mode['flags']: e['SVR06_FLAGS'] = str(mode['flags'])
    else: e.pop('SVR06_FLAGS', None)
    ic = p.get('icons') or ''
    if ic: e['SVR06_ICONSET'] = ic; e['SVR06_ICONDIR'] = os.path.join(ROOT, 'mods', 'icons')
    else: e.pop('SVR06_ICONSET', None)
    if p.get('texpack'): e['SVR06_TEXPACK'] = TEXPACK
    else: e.pop('SVR06_TEXPACK', None)
    if p.get('probes', True): e.pop('SVR06_NOPROBES', None)
    else: e['SVR06_NOPROBES'] = '1'; e.pop('SVR06_PROBES', None)
    if p.get('skipmovies'): e['SVR06_SKIPMOVIES'] = '1'
    else: e.pop('SVR06_SKIPMOVIES', None)
    try:
        lib = library()
        pr = next((d for d in lib['psp'] if d['slot_id'] == lib['psp_primary']), None)
        if pr and pr.get('root'):
            e['SVR06_PSPDIR'] = pr['root']
            if p.get('arcade') and any(f['ok'] for f in pr['features'] if f['id'] in ('gameshow', 'eugene', 'holdem')): e['SVR06_ARCADE'] = '1'
            else: e.pop('SVR06_ARCADE', None)
        else:
            e.pop('SVR06_PSPDIR', None); e.pop('SVR06_ARCADE', None)
    except Exception:
        pass
    sb = selected_build(p)
    if sb['id'] != 'pal':
        e['SVR06_BUILDPATH'] = os.path.abspath(os.path.join(ROOT, '..', sb['dir'], sb['bdir']))
        el = find_named(sb['elf'])
        if el: e['SVR06_ELF'] = el
    else:
        e.pop('SVR06_BUILDPATH', None); e.pop('SVR06_ELF', None)
    return e


def env_summary(p):
    e = build_env(p)
    out = []
    for k in ('SVR06_FLAGS', 'SVR06_ICONSET', 'SVR06_TEXPACK', 'SVR06_NOPROBES', 'SVR06_SKIPMOVIES', 'SVR06_PSPDIR', 'SVR06_ARCADE'):
        if k in e: out.append([k, os.path.basename(e[k].rstrip('\\/')) if k in ('SVR06_TEXPACK', 'SVR06_PSPDIR') else e[k]])
    return out


def count_files(d, ext):
    try:
        return sum(1 for f in os.listdir(d) if f.lower().endswith(ext))
    except OSError:
        return 0


def read_tail(path, nbytes):
    try:
        sz = os.path.getsize(path)
        with open(path, 'rb') as fh:
            fh.seek(max(0, sz - nbytes))
            return fh.read().decode('utf-8', 'replace'), sz
    except OSError:
        return '', 0


GAME_EXES = ('svr06run.exe', 'ps2entryrunner.exe')


def game_running():
    if not IS_WIN: return False
    try:
        out = subprocess.run(['tasklist', '/NH'], capture_output=True, text=True, creationflags=NOWIN, timeout=8).stdout.lower()
        return any(x in out for x in GAME_EXES)
    except Exception:
        return None                                      # unknown: keep the previous answer


_proc_cache = [0.0, False, False]


def _proc_refresh():
    try:
        r = game_running()
        if r is not None: _proc_cache[1] = r
    finally:
        _proc_cache[0] = time.time(); _proc_cache[2] = False


def game_running_cached():
    # never block a request on tasklist: refresh in the background, answer from the cache
    if time.time() - _proc_cache[0] > 3 and not _proc_cache[2]:
        _proc_cache[2] = True
        threading.Thread(target=_proc_refresh, daemon=True).start()
    return _proc_cache[1]


def heartbeat():
    hb = os.path.join(LIVE, 'heartbeat.txt')
    try:
        age = time.time() - os.path.getmtime(hb)
        with open(hb, 'r', errors='replace') as f: txt = f.read().strip()
        fields = dict(re.findall(r'(\w+)=(\S+)', txt))
        return {'age': round(age, 1), 'text': txt, 'fields': fields, 'alive': age < 20}
    except OSError:
        return None


BUILDS = [
    {'id': 'pal', 'name': 'PAL - Europe/Australia', 'serial': 'SLES-53676', 'elf': 'SLES_536.76', 'dir': 'PS2Recomp', 'bdir': 'build_svr06b_clang',
     'log': 'build_log.txt', 'rebuild': 'FAST_REBUILD_AND_RUN.cmd', 'build': 'BUILD_ONLY.cmd', 'note': 'Main build, fully playable.'},
    {'id': 'usa', 'name': 'USA - NTSC-U', 'serial': 'SLUS-21286', 'elf': 'SLUS_212.86', 'dir': 'PS2Recomp_usa', 'bdir': 'build_svr06_usa',
     'log': 'usa_build_log.txt', 'rebuild': 'USA_BUILD.cmd', 'build': 'USA_BUILD.cmd', 'note': 'First build: plain recompile of the USA executable (separate folders). PAL fixes are not ported yet, so expect gaps.'},
]
BUILD_BY_ID = {b['id']: b for b in BUILDS}


def build_exe(b):
    base = os.path.abspath(os.path.join(ROOT, '..', b['dir'], b['bdir']))
    for sub in ('ps2xRuntime', ''):
        p = os.path.join(base, sub, 'ps2EntryRunner.exe')
        if os.path.exists(p): return p
    return os.path.join(base, 'ps2xRuntime', 'ps2EntryRunner.exe')


def find_named(name):
    ad = os.path.abspath(os.path.join(ROOT, '..', 'assets'))
    for dp, dn, fn in os.walk(ad):
        if name in fn: return os.path.join(dp, name)
    return ''


def selected_build(p=None):
    p = p or load_prefs()
    for k in ('build', 'last_launched'):
        b = BUILD_BY_ID.get(p.get(k))
        if b and (k == 'build' or os.path.exists(build_exe(b))): return b
    return BUILDS[0]


def builds_info():
    out = []
    for b in BUILDS:
        ex = build_exe(b)
        out.append({'id': b['id'], 'name': b['name'], 'serial': b['serial'], 'note': b['note'], 'built': os.path.exists(ex),
                    'exe_mtime': os.path.getmtime(ex) if os.path.exists(ex) else 0, 'has_elf': bool(find_named(b['elf'])),
                    'state': build_state(b['log'])['state']})
    return out


def build_state(logname=None):
    if logname is None: logname = selected_build()['log']
    lg = os.path.join(ROOT, 'analysis', logname)
    tail, sz = read_tail(lg, 65536)
    if not sz: return {'state': 'none'}
    mt = os.path.getmtime(lg)
    done = 'BUILD STEP FINISHED' in tail
    m = re.findall(r'\[(\d+)/(\d+)\]', tail)
    n, tot = (int(m[-1][0]), int(m[-1][1])) if m else (0, 0)
    bad = bool(re.search(r'(?<![A-Za-z-])error:', tail)) or 'FAILED:' in tail
    running = (time.time() - mt) < 20 and not done
    if running: state = 'running'
    elif done and bad: state = 'failed'
    elif done: state = 'ok'
    else: state = 'interrupted'
    return {'state': state, 'n': n, 'total': tot, 'age': round(time.time() - mt), 'mtime': mt}


def exe_path(b=None):
    return build_exe(b or selected_build())


def find_elf():
    ad = os.path.abspath(os.path.join(ROOT, '..', 'assets'))
    base = ad.count(os.sep)
    for dp, dn, fn in os.walk(ad):
        if 'SLES_536.76' in fn: return os.path.join(dp, 'SLES_536.76')
        if dp.count(os.sep) - base > 3: dn[:] = []
    return None


_LR = [0.0, None]


def last_run():
    # scanning a multi-MB log is the heaviest thing the poll does; at most once every 4 s
    if time.time() - _LR[0] < 4 and _LR[1] is not None: return _LR[1]
    r = _last_run()
    _LR[0] = time.time(); _LR[1] = r
    return r


def _last_run():
    p = os.path.join(ROOT, 'analysis', 'run_err.txt')
    tx, sz = read_tail(p, 2_500_000)
    if not sz: return None
    exact = len(re.findall(r'\[texpack\] EXACT', tx))
    stats = re.findall(r'\[texpack\] stats: unique textures seen=(\d+), tex-hash hits=(\d+), exact \(tex\+palette\)=(\d+)', tx)
    ic = re.findall(r"\[iconhook\] icon set '([^']+)' loaded", tx)
    pack = re.findall(r"\[texpack\] pack '[^']*': (\d+) textures indexed", tx)
    return {'mtime': os.path.getmtime(p), 'size': sz, 'exact': exact,
            'seen': int(stats[-1][0]) if stats else 0, 'hits': int(stats[-1][1]) if stats else 0,
            'texpack_on': bool(pack), 'indexed': int(pack[-1]) if pack else 0, 'icon_set': ic[-1] if ic else '',
            'errors': len(re.findall(r'(?i)\b(fatal|exception|assert|crash)\b', tx))}


def changelog_entries(limit=40):
    out = []
    for path in sorted(glob.glob(os.path.join(ROOT, 'docs', 'CHANGELOG*.md'))):
        try:
            txt = open(path, 'r', encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        for m in re.finditer(r'^## (CL-\d+)\s*(?:\[([^\]]*)\])?\s*(?:\(([^)]*)\))?\s*[-—:/]*\s*(.*)$', txt, re.M):
            body = txt[m.end():m.end() + 900]
            body = re.split(r'\n## ', body)[0]
            bullets = [re.sub(r'[*`]', '', b.strip(' *-')) for b in body.strip().splitlines() if b.strip().startswith(('*', '-'))][:3]
            out.append({'id': m.group(1), 'ver': m.group(2) or '', 'date': m.group(3) or '', 'title': m.group(4)[:140], 'bullets': [b[:260] for b in bullets]})
        for m in re.finditer(r'^\|\s*(CL-\d+)\s*\|\s*([^|]*)\|\s*([^|]*)\|\s*([^|]*)\|', txt, re.M):
            out.append({'id': m.group(1), 'ver': '', 'date': m.group(2).strip(), 'title': m.group(4).strip()[:140], 'bullets': [], 'area': m.group(3).strip()})
    seen = {}
    for e in out: seen.setdefault(e['id'], e)
    res = sorted(seen.values(), key=lambda e: e['id'], reverse=True)
    return res[:limit]


def current_version():
    for path in sorted(glob.glob(os.path.join(ROOT, 'docs', 'CHANGELOG*.md'))):
        try:
            m = re.search(r'Current (v[\w.\-]+?)\.?\s', open(path, 'r', encoding='utf-8', errors='replace').read(4000))
            if m: return m.group(1)
        except OSError:
            pass
    return ''


def health():
    items = []
    def add(ok, title, detail='', fix=''):
        items.append({'ok': ok, 'title': title, 'detail': detail, 'fix': fix})
    ex = exe_path(BUILDS[0])
    if os.path.exists(ex):
        add(True, 'Game executable built', time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(ex))) + '  -  %.0f MB' % (os.path.getsize(ex) / 1e6))
    else:
        add(False, 'Game executable not built', fix='Click "Rebuild + play" on the Play page.')
    elf = find_elf()
    add(bool(elf), 'PAL boot file SLES_536.76', elf or 'not found under assets\\', '' if elf else 'Place the PAL game files inside the assets folder.')
    lib = library()
    for d in lib['ps2']:
        if d.get('found'): add(True if d.get('supported') else None, d['name'] + ': ' + d['serial'], d['path'], '' if d.get('supported') else 'Linked for reference; not bootable by this recomp yet.')
        else: add(None, d['name'], 'not linked', 'Optional - link it on the Games page.')
    for d in lib['psp']:
        if d.get('found'): add(True, d['name'] + ': ' + (d.get('disc_id') or d.get('kind', '')), d['path'])
        elif d['slot_id'] != 'psp_other': add(None, d['name'], 'not linked', 'Optional - needed for Arcade Mode and other PSP extras.')
    for s in ICON_SETS[1:]:
        d = os.path.join(ROOT, 'mods', 'icons', s['id'])
        ok = os.path.exists(os.path.join(d, 'glyph_idx.bin')) and os.path.exists(os.path.join(d, 'glyph_clut.bin'))
        if s['id'] == 'mypack' and not ok:
            add(None, 'Icon set: ' + s['name'], 'optional - build one with tools/make_icon_pack.py')
        else:
            add(ok, 'Icon set: ' + s['name'], '' if ok else 'files missing', '' if ok else 'Run tools/make_icon_sheets.py')
    add(os.path.exists(os.path.join(ROOT, 'mods', 'icons', 'orig_idx.bin')), 'Icon reference data', 'orig_idx.bin / orig_clut.bin used to recognise the game\'s own button sheet')
    n = count_files(TEXPACK, '.png')
    add(True if n else None, 'HD texture pack', '%d PNG files' % n if n else 'empty / not installed', '' if n else 'Copy a PCSX2-style replacement pack to ' + TEXPACK_REL)
    b = build_state(BUILDS[0]['log'])
    labels = {'ok': (True, 'finished'), 'failed': (False, 'FAILED - open Logs > Build'), 'interrupted': (None, 'interrupted or still running'), 'running': (None, 'running now'), 'none': (None, 'no build log yet')}
    ok, txt = labels[b['state']]
    add(ok, 'Last build', txt + ('  (%s ago)' % fmt_age(b['age']) if 'age' in b else ''))
    lr = last_run()
    if lr:
        add(True, 'Last game run', '%s ago  -  %d texture-pack matches, icon set: %s' % (fmt_age(time.time() - lr['mtime']), lr['exact'], lr['icon_set'] or 'original'))
        add(None if lr['errors'] else True, 'Errors in last run log', '%d suspicious lines' % lr['errors'] if lr['errors'] else 'none found')
    return items


def fmt_age(sec):
    sec = int(max(0, sec))
    if sec < 90: return '%ds' % sec
    if sec < 5400: return '%dm' % (sec // 60)
    if sec < 172800: return '%.1fh' % (sec / 3600)
    return '%dd' % (sec // 86400)


def list_shots(limit=60):
    out = []
    for rel in (os.path.join('analysis', 'live', 'shots'), os.path.join('analysis', 'shots')):
        d = os.path.join(ROOT, rel)
        try:
            for f in os.listdir(d):
                if f.lower().endswith(('.png', '.jpg', '.jpeg')):
                    p = os.path.join(d, f)
                    out.append({'name': f, 'path': (rel + os.sep + f).replace('\\', '/'), 'mtime': os.path.getmtime(p), 'size': os.path.getsize(p)})
        except OSError:
            pass
    out.sort(key=lambda e: e['mtime'], reverse=True)
    return out[:limit]


def read_log(src, flt, tail_bytes=3_000_000, max_lines=1500):
    allowed = {'run_err': 'run_err.txt', 'run_out': 'run_out.txt', 'build': 'build_log.txt'}
    path = os.path.join(ROOT, 'analysis', allowed.get(src, 'run_err.txt'))
    tx, sz = read_tail(path, tail_bytes)
    lines = tx.splitlines()
    total = len(lines)
    pats = [x for x in re.split(r'\|', flt or '') if x]
    if pats:
        low = [x.lower() for x in pats]
        lines = [l for l in lines if any(x in l.lower() for x in low)]
    return {'lines': lines[-max_lines:], 'matched': len(lines), 'total': total, 'size': sz, 'mtime': os.path.getmtime(path) if sz else 0}


# ---------------------------------------------------------------- game library (all versions)
PS2_SLOTS = [
    {'id': 'ps2_pal', 'name': 'PlayStation 2 - Europe / PAL', 'region': 'PAL', 'sub': 'The version this recomp is built from'},
    {'id': 'ps2_usa', 'name': 'PlayStation 2 - North America', 'region': 'NTSC-U', 'sub': 'The HD texture pack was made for this one'},
    {'id': 'ps2_jpn', 'name': 'PlayStation 2 - Japan', 'region': 'NTSC-J', 'sub': 'Released as Exciting Pro Wres'},
]
PSP_SLOTS = [
    {'id': 'psp_eu', 'name': 'PSP - Europe', 'region': 'PAL', 'sub': 'En / Fr / De / Es / It'},
    {'id': 'psp_us', 'name': 'PSP - North America', 'region': 'NTSC-U', 'sub': ''},
    {'id': 'psp_kr', 'name': 'PSP - Korea', 'region': 'KOR', 'sub': ''},
    {'id': 'psp_other', 'name': 'PSP - Other version', 'region': '?', 'sub': 'Any other PSP release'},
]
SUPPORTED_SERIAL = 'SLES_536.76'
PSP_REGION_SLOT = {'ULES': 'psp_eu', 'UCES': 'psp_eu', 'ULUS': 'psp_us', 'UCUS': 'psp_us', 'ULKS': 'psp_kr', 'UCKS': 'psp_kr'}
PS2_REGION_SLOT = {'SLES': 'ps2_pal', 'SCES': 'ps2_pal', 'SLUS': 'ps2_usa', 'SCUS': 'ps2_usa', 'SLPM': 'ps2_jpn', 'SLPS': 'ps2_jpn', 'SCPS': 'ps2_jpn', 'SCPM': 'ps2_jpn'}
IMAGE_EXT = ('.iso', '.bin', '.img', '.cso', '.chd', '.mdf', '.nrg')
_LIB = {'t': 0, 'v': None}


def _walk(base, depth):
    b = base.rstrip('\\/').count(os.sep)
    for dp, dn, fn in os.walk(base):
        yield dp, dn, fn
        if dp.count(os.sep) - b >= depth: dn[:] = []


_D2 = {}


def detect_ps2(path):
    # disc images are read once (24 MB) and remembered while the file is unchanged
    try: key = (path, os.path.getmtime(path), os.path.getsize(path) if os.path.isfile(path) else 0)
    except OSError: key = None
    if key and key in _D2: return dict(_D2[key])
    r = _detect_ps2(path)
    if key: _D2[key] = dict(r)
    return r


def _detect_ps2(path):
    r = {'found': False, 'path': path}
    if not path or not os.path.exists(path): return r
    ser = None
    if os.path.isfile(path):
        r['kind'] = 'disc image' if path.lower().endswith(IMAGE_EXT) else 'file'
        try:
            with open(path, 'rb') as fh: blob = fh.read(24 * 1024 * 1024)
            m = re.search(rb'BOOT2\s*=\s*cdrom0:\\?([A-Z]{4}_\d{3}\.\d{2})', blob)
            if m: ser = m.group(1).decode()
        except OSError:
            pass
    else:
        r['kind'] = 'extracted folder'
        for dp, dn, fn in _walk(path, 3):
            if 'SYSTEM.CNF' in [f.upper() for f in fn]:
                try:
                    txt = open(os.path.join(dp, [f for f in fn if f.upper() == 'SYSTEM.CNF'][0]), 'r', errors='replace').read()
                    m = re.search(r'BOOT2\s*=\s*cdrom0:\\?([A-Z]{4}_\d{3}\.\d{2})', txt)
                    if m: ser = m.group(1); break
                except OSError:
                    pass
            for f in fn:
                if re.fullmatch(r'S[CL][A-Z]{2}_\d{3}\.\d{2}', f.upper()): ser = f.upper(); break
            if ser: break
    if not ser: r['note'] = 'No PS2 boot file found here.'; return r
    r.update(found=True, serial=ser, supported=(ser == SUPPORTED_SERIAL), slot=PS2_REGION_SLOT.get(ser[:4]))
    r['note'] = ('Runs on the recompiled executable.' if r['supported'] else
                 'Recognised, but this recomp is built from the PAL executable (' + SUPPORTED_SERIAL + '). Another region needs its own recompile pass; the data is linked here so it can be used for asset swaps and comparisons.')
    return r


def parse_sfo(fp):
    try:
        d = open(fp, 'rb').read(65536)
        import struct
        if d[:4] != b'\0PSF': return {}
        kt, dt, n = struct.unpack_from('<III', d, 8)
        out = {}
        for i in range(n):
            ko, fmt, ln, mx, do = struct.unpack_from('<HHIII', d, 20 + 16 * i)
            k = d[kt + ko:d.index(b'\0', kt + ko)].decode('ascii', 'replace')
            v = d[dt + do:dt + do + ln]
            out[k] = v.rstrip(b'\0').decode('utf-8', 'replace') if fmt in (0x0004, 0x0204) else ''
        return out
    except Exception:
        return {}


PSP_FEATURES = [
    ('gameshow', 'Arcade: WWE Game Show', 'Quiz movies (50 clips) and the host voice pack'),
    ('eugene', 'Arcade: Eugene Airplane Race', 'Opening and goal movies'),
    ('holdem', 'Arcade: Texas Hold\'em', 'Card, table and menu text packs (zmisc)'),
    ('season', 'Season music', 'Extra background music packs (zssnd0-5)'),
    ('titantron', 'Extra entrance movies', 'PSP titantron PMF files'),
]


def detect_psp(path):
    r = {'found': False, 'path': path}
    if not path or not os.path.exists(path): return r
    if os.path.isfile(path):
        r.update(kind='disc image' if path.lower().endswith(IMAGE_EXT) else 'file', found=path.lower().endswith(IMAGE_EXT),
                 note='This is a UMD image. Extract it to a folder first (the PSP_GAME folder is what the launcher reads).', features=[])
        return r
    pg = None
    for dp, dn, fn in _walk(path, 2):
        if os.path.basename(dp).upper() == 'PSP_GAME': pg = dp; break
    if not pg: r['note'] = 'No PSP_GAME folder found here.'; return r
    sfo = parse_sfo(os.path.join(pg, 'PARAM.SFO'))
    did = sfo.get('DISC_ID', '')
    usr = os.path.join(pg, 'USRDIR')
    rel = set()
    for dp, dn, fn in _walk(usr, 4):
        for f in fn: rel.add(os.path.relpath(os.path.join(dp, f), usr).replace('\\', '/').lower())
    def n(prefix, ext=''): return sum(1 for x in rel if x.startswith(prefix) and x.endswith(ext))
    feats = {
        'gameshow': (n('movie/quizshow/', '.pmf') >= 50 and 'bgm/zquizvoice.pac' in rel, '%d clips, voice pack %s' % (n('movie/quizshow/', '.pmf'), 'yes' if 'bgm/zquizvoice.pac' in rel else 'missing')),
        'eugene': (n('movie/eugene_', '.pmf') >= 2, '%d movies' % n('movie/eugene_', '.pmf')),
        'holdem': ('zpac/zmisc.pac' in rel, 'zmisc.pac ' + ('present' if 'zpac/zmisc.pac' in rel else 'missing')),
        'season': (n('bgm/season/', '.pac') >= 6, '%d of 6 packs' % n('bgm/season/', '.pac')),
        'titantron': (n('movie/', '.pmf') >= 60, '%d PMF movies' % n('movie/', '.pmf')),
    }
    r.update(found=True, kind='extracted folder', root=os.path.dirname(pg), disc_id=did, title=sfo.get('TITLE', ''), slot=PSP_REGION_SLOT.get(did[:4]),
             files=len(rel), decrypted=os.path.exists(os.path.join(pg, 'SYSDIR', 'EBOOT_decrypted.elf')),
             features=[{'id': i, 'name': nm, 'what': what, 'ok': feats[i][0], 'detail': feats[i][1]} for i, nm, what in PSP_FEATURES])
    r['note'] = '%s  -  %d files' % (did or 'unknown id', len(rel))
    return r


_LIB_BUSY = [False]


def library(force=False):
    # stale-while-revalidate: the folder scan is slow on big libraries, so a poll never waits for it
    if not force and _LIB['v'] is not None:
        if time.time() - _LIB['t'] >= 90 and not _LIB_BUSY[0]:
            _LIB_BUSY[0] = True
            def bg():
                try: _library_scan()
                finally: _LIB_BUSY[0] = False
            threading.Thread(target=bg, daemon=True).start()
        return _LIB['v']
    return _library_scan()


def _library_scan():
    p = load_prefs(); links = dict(p.get('games') or {})
    assets = os.path.abspath(os.path.join(ROOT, '..', 'assets'))
    auto = {}
    elf = find_elf()
    if elf: auto['ps2_pal'] = os.path.dirname(elf)
    for dp, dn, fn in (_walk(assets, 3) if os.path.isdir(assets) else []):
        for f in fn:
            m = re.fullmatch(r'(S[CL][A-Z]{2})_\d{3}\.\d{2}', f.upper())
            if m and PS2_REGION_SLOT.get(m.group(1)) and PS2_REGION_SLOT[m.group(1)] not in auto: auto[PS2_REGION_SLOT[m.group(1)]] = dp
    pspdir = os.path.join(assets, 'PSP')
    if os.path.isdir(pspdir):
        for d in sorted(os.listdir(pspdir)):
            fp = os.path.join(pspdir, d)
            if os.path.isdir(fp):
                r = detect_psp(fp)
                if r.get('found') and r.get('slot') and r['slot'] not in auto: auto[r['slot']] = fp
    out = {'ps2': [], 'psp': []}
    for kind, slots, fn in (('ps2', PS2_SLOTS, detect_ps2), ('psp', PSP_SLOTS, detect_psp)):
        for s in slots:
            path = links.get(s['id']) or auto.get(s['id']) or ''
            d = fn(path) if path else {'found': False, 'path': ''}
            d.update(slot_id=s['id'], name=s['name'], region=s['region'], sub=s['sub'], linked=bool(links.get(s['id'])), auto=bool(path and not links.get(s['id'])))
            out[kind].append(d)
    prim = psp_primary(out['psp'], p)
    out['psp_primary'] = prim['slot_id'] if prim else ''
    _LIB['t'] = time.time(); _LIB['v'] = out
    return out


def psp_primary(psp, p):
    want = p.get('psp_primary') or ''
    ok = [d for d in psp if d.get('found') and d.get('features')]
    for d in ok:
        if d['slot_id'] == want: return d
    return ok[0] if ok else None


def browse_folder(kind):
    if not IS_WIN: return ''
    ps = ("Add-Type -AssemblyName System.Windows.Forms;$d=New-Object System.Windows.Forms.FolderBrowserDialog;"
          "$d.Description='Pick the game folder';$f=New-Object System.Windows.Forms.Form;$f.TopMost=$true;"
          "if($d.ShowDialog($f) -eq 'OK'){[Console]::Out.Write($d.SelectedPath)}")
    try:
        return subprocess.run(['powershell', '-NoProfile', '-STA', '-Command', ps], capture_output=True, text=True, creationflags=NOWIN, timeout=300).stdout.strip()
    except Exception:
        return ''


def games_op(body):
    op = body.get('op'); slot = body.get('slot', '')
    p = load_prefs(); g = dict(p.get('games') or {})
    valid = [s['id'] for s in PS2_SLOTS + PSP_SLOTS]
    msg = ''
    if op == 'browse':
        return {'path': browse_folder(slot)}
    if op in ('link', 'unlink', 'primary', 'rescan'):
        if op == 'link':
            if slot not in valid: return {'message': 'Unknown slot'}
            path = (body.get('path') or '').strip().strip('"')
            if not path or not os.path.exists(path): return {'message': 'That path does not exist'}
            det = (detect_ps2 if slot.startswith('ps2') else detect_psp)(path)
            g[slot] = path; msg = ('Linked: ' + (det.get('serial') or det.get('disc_id') or 'recognised')) if det.get('found') else 'Linked, but nothing recognisable was found there: ' + det.get('note', '')
        elif op == 'unlink':
            g.pop(slot, None); msg = 'Link removed'
        elif op == 'primary':
            p['psp_primary'] = slot; msg = 'PSP data source set'
        else:
            msg = 'Rescanned'
        p['games'] = g
        save_prefs(p)
    return {'message': msg, 'library': library(True)}



# ---------------------------------------------------------------- cheats
# status: 'working' = address known and verified, 'untested' = implemented but not checked in game, 'todo' = address not mapped yet
SAVE_BASE = 0x3D9BF8                       # profile save buffer in EE RAM
SHOP_TABLE, SHOP_STRIDE, SHOP_COUNT = 0x591FF8, 0x28, 137
BITMAP_OFFS = (0x6A38C, 0x6A39E)           # owned-item bitmaps (S0 + mirror S1), bit n = shop table entry n
CHEATS = [
    {'g': 'Match', 'id': 'auto_reversal', 'name': 'Auto reversal', 'desc': 'Every incoming attack is reversed.', 'per': True, 'kind': 'switch', 'status': 'todo'},
    {'g': 'Match', 'id': 'god_mode', 'name': 'God mode (no damage)', 'desc': 'Health never goes down.', 'per': True, 'kind': 'switch', 'status': 'todo'},
    {'g': 'Match', 'id': 'finisher_start', 'name': 'Finishers at match start', 'desc': 'Start every match with this many finishers stored.', 'per': True, 'kind': 'choice', 'opts': ['off', '1', '2', 'unlimited'], 'status': 'todo'},
    {'g': 'Match', 'id': 'finisher_always', 'name': 'Finishers at all times', 'desc': 'Keep the stored finisher count at this level.', 'per': True, 'kind': 'choice', 'opts': ['off', '1', '2', 'unlimited'], 'status': 'todo'},
    {'g': 'Match', 'id': 'divas_as_men', 'name': 'Fight Divas as men', 'desc': 'Allow Divas in men\'s matches and the other way round.', 'kind': 'switch', 'status': 'todo'},
    {'g': 'Match', 'id': 'rumble_meter', 'name': 'Instant-deplete elimination meter', 'desc': 'Royal Rumble: the elimination meter empties at once.', 'kind': 'switch', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_all', 'name': 'Unlock all', 'desc': 'Marks every shop item (characters, legends, costumes, decorations, trophies, loading screens) as owned.', 'kind': 'switch', 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'free_shop', 'name': 'Everything costs $0', 'desc': 'Sets every shop price to zero. Your cash display is not changed (the cash address is not mapped yet).', 'kind': 'switch', 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'infinite_money', 'name': 'Infinite money (cash value)', 'desc': 'Keeps your cash at the maximum.', 'kind': 'switch', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_trophy', 'name': 'Unlock single trophy', 'desc': 'Pick one trophy by name.', 'kind': 'pick', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_loading', 'name': 'Unlock single loading screen', 'desc': 'Pick one by name.', 'kind': 'pick', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_char', 'name': 'Unlock single buyable character', 'desc': 'Pick one by name.', 'kind': 'pick', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_costume', 'name': 'Unlock single costume (alt attire)', 'desc': 'Pick one by name.', 'kind': 'pick', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_legend', 'name': 'Unlock single non-buyable legend', 'desc': 'Stone Cold, The Rock, Jake The Snake and others, by name.', 'kind': 'pick', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_deco', 'name': 'Unlock single locker room decoration', 'desc': 'Pick one by name.', 'kind': 'pick', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_npc', 'name': 'Make NPCs playable', 'desc': 'Referees, managers and other non-roster people selectable in matches. Missing portraits and renders get fixed later.', 'kind': 'switch', 'status': 'todo'},
    {'g': 'GM Mode', 'id': 'gm_five_stars', 'name': 'Shows and matches always get 5 stars', 'desc': '', 'kind': 'switch', 'status': 'todo'},
    {'g': 'GM Mode', 'id': 'gm_money', 'name': 'Unlimited GM money', 'desc': '', 'kind': 'switch', 'status': 'todo'},
    {'g': 'GM Mode', 'id': 'gm_trades', 'name': 'Trades always accepted', 'desc': 'Not when another human is in control (single player against the computer only).', 'kind': 'switch', 'status': 'todo'},
    {'g': 'GM Mode', 'id': 'gm_no_fatigue', 'name': 'Superstars cannot get fatigue or injuries', 'desc': '', 'kind': 'switch', 'status': 'todo'},
    {'g': 'Season Mode', 'id': 'season_win_skip', 'name': 'Always win when skipping a match', 'desc': '', 'kind': 'switch', 'status': 'todo'},
    {'g': 'General', 'id': 'savestates', 'name': 'Save / load states', 'desc': 'Needs support inside the game runtime, not a memory poke.', 'kind': 'switch', 'status': 'todo'},
]
CHEAT_BY_ID = {c['id']: c for c in CHEATS}
_CH = {'applied': {}, 'msg': '', 'last': 0.0, 'lock': threading.Lock()}


def console(lines, wait=3.0):
    """Send console lines to the running game and return what it printed."""
    if not svrlive: return None
    out = svrlive.OUT
    before = os.path.getsize(out) if os.path.exists(out) else 0
    svrlive.send(lines, 8)
    t0 = time.time(); txt = ''
    while time.time() - t0 < wait:
        time.sleep(0.2)
        try:
            with open(out, 'r', errors='replace') as f:
                f.seek(before); txt = f.read()
        except OSError:
            txt = ''
        if txt.count('> ') >= len(lines): break
    return txt


def peek32(addr):
    t = console(['ee32 %x 1' % addr])
    m = re.search(r'^%x:\s*([0-9a-fA-F]+)\s*$' % addr, t or '', re.M | re.I)
    return int(m.group(1), 16) if m else None


def apply_cheats(p):
    """Write the enabled cheats into the running game. Returns a short status text."""
    ch = p.get('cheats') or {}
    lines = []
    if ch.get('unlock_all'):
        for off in BITMAP_OFFS:
            a = SAVE_BASE + off
            lines += ['poke32 %x fffffff8' % a, 'poke32 %x ffffffff' % (a + 4), 'poke32 %x ffffffff' % (a + 8), 'poke32 %x ffffffff' % (a + 12)]
            w = peek32(a + 16)
            if w is None: return 'could not read the game (is it in a menu or match?)'
            lines.append('poke32 %x %x' % (a + 16, (w & 0xFFFF0000) | 0x01FF))
    if ch.get('unlock_all') or ch.get('free_shop'):
        for i in range(3, SHOP_COUNT):
            b = SHOP_TABLE + i * SHOP_STRIDE
            if ch.get('unlock_all'): lines += ['poke32 %x 1' % (b + 7 * 4), 'poke32 %x 0' % (b + 9 * 4)]
            if ch.get('free_shop'): lines.append('poke32 %x 0' % (b + 8 * 4))
    if not lines: return 'nothing to apply'
    for i in range(0, len(lines), 60): console(lines[i:i + 60], 2.0)
    return 'applied %d writes' % len(lines)


def cheat_loop():
    last_sig = None; last_t = 0.0
    while True:
        time.sleep(1.0)
        try:
            p = load_prefs(); hb = heartbeat()
            if not (hb and hb['alive']): last_sig = None; continue
            en = {k: v for k, v in (p.get('cheats') or {}).items() if v and v != 'off' and CHEAT_BY_ID.get(k, {}).get('status') in ('working', 'untested')}
            if not en: _CH['msg'] = ''; last_sig = None; continue
            sig = json.dumps(en, sort_keys=True)
            if sig != last_sig or time.time() - last_t > 6:          # re-apply every few seconds (menus reload the tables)
                with _CH['lock']: _CH['msg'] = apply_cheats(p)
                last_sig = sig; last_t = time.time()
        except Exception as e:
            _CH['msg'] = 'error: ' + str(e)[:100]


# ---------------------------------------------------------------- actions
def spawn(cmd):
    p = load_prefs()
    subprocess.Popen(cmd, shell=True, cwd=ROOT, env=build_env(p))


def do_action(name, arg, body):
    sc = os.path.join(ROOT, 'scripts')
    if name in ('play', 'rebuild', 'build'):
        if arg in BUILD_BY_ID:
            pp = load_prefs(); pp['build'] = arg; save_prefs(pp)
        b = selected_build()
        if name == 'play':
            if not os.path.exists(build_exe(b)): return b['name'] + ' is not built yet - use Rebuild + play.'
            pp = load_prefs(); pp['last_launched'] = b['id']; save_prefs(pp)
            spawn('"%s"' % os.path.join(sc, 'run.cmd')); return 'Starting ' + b['name'] + '...'
        if name == 'rebuild':
            pp = load_prefs(); pp['last_launched'] = b['id']; save_prefs(pp)
            spawn('"%s"%s' % (os.path.join(sc, b['rebuild']), ' -Launch' if b['id'] != 'pal' else '')); return 'Rebuilding ' + b['name'] + ', the game starts when it is done.'
        spawn('"%s"' % os.path.join(sc, b['build'])); return 'Building ' + b['name'] + ' only (no launch).'
    if name == 'quit':
        r = None
        if svrlive:
            try: r = svrlive.send(['quit'], 6)
            except Exception: r = None
        if IS_WIN: subprocess.Popen('taskkill /IM svr06run.exe & taskkill /IM ps2EntryRunner.exe', shell=True, creationflags=NOWIN)
        return 'Quit sent' + ('' if r else ' (no answer, used taskkill)')
    if name in ('shot', 'perf'):
        if not svrlive: return 'Live console not available'
        cmd = 'shot launcher_' + time.strftime('%H%M%S') if name == 'shot' else 'perf'
        r = svrlive.send([cmd], 12)
        return (r or 'No answer - is the game running?').strip()[:300]
    if name == 'scenario':
        sp = os.path.join(TOOLS, 'scenarios', os.path.basename(arg or ''))
        if not os.path.isfile(sp): return 'Scenario not found'
        spawn('python "%s" "%s"' % (os.path.join(TOOLS, 'scenario.py'), sp)); return 'Scenario started: ' + os.path.basename(sp)
    if name == 'report':
        return make_report()
    if name == 'prune':
        d = os.path.join(ROOT, 'analysis')
        fs = sorted(f for f in os.listdir(d) if f.startswith('prev_') and f.endswith(('_run_err.txt', '_run_out.txt')))
        n = 0
        for f in fs[:-20]:
            try: os.remove(os.path.join(d, f)); n += 1
            except OSError: pass
        return 'Removed %d of %d old logs (kept the newest 20 sets).' % (n, len(fs))
    if name == 'open':
        targets = {'root': ROOT, 'logs': os.path.join(ROOT, 'analysis'), 'shots': os.path.join(ROOT, 'analysis', 'shots'),
                   'icons': os.path.join(ROOT, 'mods', 'icons'), 'texpack': os.path.join(ROOT, 'mods', 'texturepack'),
                   'changelog': os.path.join(ROOT, 'docs', 'CHANGELOG-2026-10-28.md'), 'mods': os.path.join(ROOT, 'mods')}
        t = targets.get(arg)
        if not t: return 'Unknown folder'
        if not os.path.exists(t) and not t.endswith('.md'): os.makedirs(t, exist_ok=True)
        if IS_WIN: os.startfile(t)
        else: return 'Open: ' + t
        return 'Opened ' + t
    return 'Unknown action'


def make_report():
    import zipfile
    out = os.path.join(ROOT, 'analysis', 'bug_report_%s.zip' % time.strftime('%Y%m%d_%H%M%S'))
    n = 0
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('INFO.txt', 'prefs=%s\ntime=%s\nversion=%s\n' % (json.dumps(load_prefs()), time.ctime(), current_version()))
        for d in (LIVE, os.path.join(ROOT, 'analysis'), os.path.join(ROOT, 'analysis', 'scenario')):
            if not os.path.isdir(d): continue
            for f in sorted(os.listdir(d)):
                p = os.path.join(d, f)
                if os.path.isfile(p) and f.lower().endswith(('.log', '.txt', '.zip')) and not f.startswith('prev_') and os.path.getsize(p) < 50_000_000:
                    z.write(p, os.path.basename(d) + '/' + f); n += 1
        sh = os.path.join(LIVE, 'shots')
        if os.path.isdir(sh):
            for f in sorted(os.listdir(sh))[-5:]:
                z.write(os.path.join(sh, f), 'shots/' + f); n += 1
    return 'Bug report saved: %s (%d files)' % (os.path.basename(out), n)


def state():
    p = load_prefs()
    lr = last_run()
    ex = exe_path()
    return {
        'now': time.time(), 'version': current_version(), 'prefs': p, 'env': env_summary(p),
        'modes': MODES, 'icon_sets': [dict(s, ready=(not s['id']) or os.path.exists(os.path.join(ROOT, 'mods', 'icons', s['id'], 'glyph_idx.bin'))) for s in ICON_SETS],
        'texpack': {'count': count_files(TEXPACK, '.png'), 'path': TEXPACK_REL, 'stage': 'detect'},
        'builds': builds_info(), 'sel_build': selected_build(p)['id'],
        'game': {'running': game_running_cached(), 'heartbeat': heartbeat(), 'exe_mtime': os.path.getmtime(ex) if os.path.exists(ex) else 0},
        'build': build_state(), 'last_run': lr, 'scenarios': sorted(os.listdir(os.path.join(TOOLS, 'scenarios'))) if os.path.isdir(os.path.join(TOOLS, 'scenarios')) else [],
        'cheats': {'defs': CHEATS, 'msg': _CH['msg']}, 'library': library(), 'root': ROOT, 'platform': 'windows' if IS_WIN else sys.platform,
    }


# ---------------------------------------------------------------- http
class Handler(BaseHTTPRequestHandler):
    server_version = 'SvR06Launcher/2'

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
            if path == '/favicon.ico':
                return self._send(204, b'', 'image/x-icon')
            if path.startswith('/ui/'):
                fp = os.path.normpath(os.path.join(UI, path[4:]))
                if fp.startswith(UI) and os.path.isfile(fp):
                    return self._send(200, open(fp, 'rb').read(), mimetypes.guess_type(fp)[0] or 'application/octet-stream')
                return self._send(404, {'error': 'not found'})
            if not self._authed(q): return self._send(403, {'error': 'bad token'})
            if path == '/api/state': return self._send(200, state())
            if path == '/api/log': return self._send(200, read_log((q.get('src') or ['run_err'])[0], (q.get('f') or [''])[0]))
            if path == '/api/health': return self._send(200, health())
            if path == '/api/shots': return self._send(200, list_shots())
            if path == '/api/changelog': return self._send(200, changelog_entries())
            if path == '/api/ping':
                LAST_PING[0] = time.time(); return self._send(200, {'ok': True})
            if path == '/file':
                rel = (q.get('p') or [''])[0].replace('\\', '/')
                fp = os.path.normpath(os.path.join(ROOT, rel))
                ok = any(fp.startswith(os.path.normpath(os.path.join(ROOT, d)) + os.sep) for d in SERVE_DIRS)
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
            if u.path == '/api/prefs':
                p = load_prefs(); p.update({k: v for k, v in body.items() if k in DEFAULT_PREFS})
                return self._send(200, {'prefs': save_prefs(p), 'env': env_summary(p)})
            if u.path == '/api/action':
                return self._send(200, {'message': do_action(body.get('name', ''), body.get('arg', ''), body)})
            if u.path == '/api/games':
                r = games_op(body); r['env'] = env_summary(load_prefs()); return self._send(200, r)
            if u.path == '/api/exit':
                threading.Thread(target=lambda: (time.sleep(0.2), os._exit(0)), daemon=True).start()
                return self._send(200, {'ok': True})
            return self._send(404, {'error': 'unknown'})
        except Exception as e:
            return self._send(500, {'error': str(e)})


def make_server(port=0):
    threading.Thread(target=cheat_loop, daemon=True).start()
    return ThreadingHTTPServer(('127.0.0.1', port), Handler)
