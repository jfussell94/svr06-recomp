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
LIVE_PAL = LIVE


def cur_live():
    """Live-console folder of the game that is (or was last) running: each version runs from its own build folder, so each has its own analysis/live."""
    global LIVE
    try:
        p = load_prefs(); b = BUILD_BY_ID.get(p.get('last_launched')) or selected_build(p)
        d = os.path.join(os.path.abspath(os.path.join(ROOT, '..', b['dir'], b['bdir'])), 'analysis', 'live') if b['id'] != 'pal' else LIVE_PAL
    except Exception:
        d = LIVE_PAL
    LIVE = d
    if svrlive:
        svrlive.LIVE = d; svrlive.OUT = os.path.join(d, 'out.txt')
    return d
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
                 'games': {}, 'psp_primary': '', 'arcade': False, 'cheats': {}, 'per_build': {}, 'build': '', 'last_launched': '', 'card_format': 'folder', 'card_file': '', 'gh_repo': 'jfussell94/svr06-recomp', 'tickets': []}
# Settings that change how a game runs are stored PER GAME VERSION (prefs['per_build'][build_id]); the rest (theme, GitHub, library ...) is global.
# load_prefs() resolves these keys for the selected build, so the rest of the launcher keeps reading p['mode'], p['icons'] ...
GAME_KEYS = ('mode', 'icons', 'texpack', 'probes', 'skipmovies', 'arcade')
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
        c = p.get('cheats')
        if isinstance(c, dict) and any(not isinstance(v, dict) for v in c.values()):
            p['cheats'] = {'pal': c}       # legacy flat cheat settings were made on the PAL build
        if 'per_build' not in raw:         # legacy flat game settings were made on the PAL build; other versions start from defaults
            p['per_build'] = {'pal': {k: raw[k] for k in GAME_KEYS if k in raw}}
    except Exception:
        pass
    if not isinstance(p.get('per_build'), dict): p['per_build'] = {}
    try:
        pb = p['per_build'].get(selected_build(p)['id']) or {}
    except Exception:
        pb = {}
    for k in GAME_KEYS: p[k] = pb.get(k, DEFAULT_PREFS[k])
    return p


def set_selected_build(bid):
    """Switch the selected version without moving the previous version's settings onto it."""
    raw = {}
    try:
        with open(PREFS, 'r', encoding='utf-8') as f: raw = json.load(f)
    except Exception: pass
    raw['build'] = bid
    os.makedirs(os.path.dirname(PREFS), exist_ok=True)
    with open(PREFS, 'w', encoding='utf-8') as f: json.dump(raw, f, indent=1)


def save_prefs(p):
    os.makedirs(os.path.dirname(PREFS), exist_ok=True)
    clean = {k: p.get(k, DEFAULT_PREFS[k]) for k in DEFAULT_PREFS}
    pbs = dict(p.get('per_build') or {})
    try: pbs[selected_build(p)['id']] = {k: p[k] for k in GAME_KEYS if k in p}
    except Exception: pass
    clean['per_build'] = pbs
    for k in GAME_KEYS: clean.pop(k, None)
    with open(PREFS, 'w', encoding='utf-8') as f:
        json.dump(clean, f, indent=1)
    return clean


def build_env(p):
    e = dict(os.environ)
    mode = next((m for m in MODES if m['id'] == p.get('mode')), MODES[0])
    if mode['flags']: e['SVR06_FLAGS'] = str(mode['flags'])
    else: e.pop('SVR06_FLAGS', None)
    e['SVR06_ICONDIR'] = os.path.join(ROOT, 'mods', 'icons'); e['SVR06_TEXPACK_DIR'] = TEXPACK
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
    e['SVR06_MCFORMAT'] = p.get('card_format') or 'folder'      # 'folder' = native host folder card (supported); 'pcsx2' = .ps2 card image (runtime support not built yet)
    if p.get('card_format') == 'pcsx2' and p.get('card_file'): e['SVR06_MCFILE'] = p['card_file']
    else: e.pop('SVR06_MCFILE', None)
    sb = selected_build(p)
    e['SVR06_LOGTAG'] = sb['id']      # runcap.ps1 writes run_err_<id>.txt for non-PAL versions
    if sb['id'] != 'pal':
        # controller-icon sets and the HD texture pack are matched against PAL memory addresses / texture hashes: on other versions they corrupt text and textures
        e.pop('SVR06_ICONSET', None); e.pop('SVR06_TEXPACK', None)
        e.setdefault('SVR06_GSDUMPTICK', '4800'); e['SVR06_GSDUMPLEN'] = '20'      # USA bring-up: capture the GS packets of ~20 frames at ~80 s so a broken scene can be analysed
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


GAME_EXES = ('svr06run.exe', 'ps2entryrunner.exe', 'svr06_pal.exe', 'svr06_usa.exe', 'svr06_ita.exe', 'svr06_jpn.exe', 'svr06_kor.exe')


def exe_of(bid):
    return 'SVR06_%s.exe' % str(bid or 'pal').upper()


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
    hb = os.path.join(cur_live(), 'heartbeat.txt')
    try:
        age = time.time() - os.path.getmtime(hb)
        with open(hb, 'r', errors='replace') as f: txt = f.read().strip()
        fields = dict(re.findall(r'(\w+)=(\S+)', txt))
        return {'age': round(age, 1), 'text': txt, 'fields': fields, 'alive': age < 20}
    except OSError:
        return None


BUILDS = [
    {'id': 'pal', 'name': 'PAL - Europe/Australia', 'serial': 'SLES-53676', 'elf': 'SLES_536.76', 'dir': 'PS2Recomp', 'bdir': 'build_svr06b_clang',
     'log': 'build_log.txt', 'rebuild': 'FAST_REBUILD_AND_RUN.cmd', 'build': 'BUILD_ONLY.cmd', 'note': 'Main build. Matches run; known crashes and glitches remain, so it is not release quality yet.'},
    {'id': 'usa', 'name': 'USA - NTSC-U', 'serial': 'SLUS-21286', 'elf': 'SLUS_212.86', 'dir': 'PS2Recomp_usa', 'bdir': 'build_svr06_usa',
     'log': 'usa_build_log.txt', 'rebuild': 'USA_BUILD.cmd', 'build': 'USA_BUILD.cmd', 'note': 'Early build in separate folders. The PAL fixes are being ported over; expect a black screen or stalls before the menus.'},
]
def _load_regions():
    try:
        with open(os.path.join(ROOT, 'regions', 'regions.json'), 'r', encoding='utf-8') as f: return json.load(f)['regions']
    except Exception:
        return []


def _load_states():
    try:
        with open(os.path.join(ROOT, 'regions', 'regions.json'), 'r', encoding='utf-8') as f: return json.load(f).get('states', [])
    except Exception:
        return []
STATES = _load_states() or [{'id': 'planned', 'label': 'Planned', 'desc': ''}, {'id': 'bootable', 'label': 'Bootable', 'desc': ''}, {'id': 'ingame', 'label': 'In-game', 'desc': ''}]
STATE_LABEL = {s_['id']: s_['label'] for s_ in STATES}
REGIONS = _load_regions()
REGION_BY_ID = {r_['id']: r_ for r_ in REGIONS}
for _r in REGIONS:                                  # planned releases (Italy / Japan / Korea ...) are listed but cannot be built yet
    if _r['id'] in ('pal', 'usa'): continue
    BUILDS.append({'id': _r['id'], 'name': _r['short'] + ' (planned)', 'serial': _r['serial'], 'elf': _r['boot_elf'], 'dir': _r['runtime_dir'], 'bdir': _r['build_dir'],
                   'log': _r['id'] + '_build_log.txt', 'rebuild': '', 'build': '', 'planned': True, 'note': 'Planned - not started yet. Listed so the version has a place in the project.'})
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
        if b and not b.get('planned') and (k == 'build' or os.path.exists(build_exe(b))): return b
    return BUILDS[0]


def builds_info():
    out = []
    for b in BUILDS:
        ex = build_exe(b)
        out.append({'id': b['id'], 'name': b['name'], 'serial': b['serial'], 'note': b['note'], 'built': os.path.exists(ex),
                    'exe_mtime': os.path.getmtime(ex) if os.path.exists(ex) else 0, 'has_elf': bool(find_named(b['elf'])), 'planned': bool(b.get('planned')),
                    'state': build_state(b['log'])['state'],
                    'stage': (REGION_BY_ID.get(b['id']) or {}).get('state', 'planned' if b.get('planned') else 'bootable'),
                    'stage_label': STATE_LABEL.get((REGION_BY_ID.get(b['id']) or {}).get('state', 'planned' if b.get('planned') else 'bootable'), ''),
                    'tags': (REGION_BY_ID.get(b['id']) or {}).get('tags', [])})
    return out


_link_cache = [0.0, False]
def linker_running():
    """True while the C++ linker (lld-link / link) is running: the build log is silent during linking, so this is the only sign of it."""
    if not IS_WIN: return False
    if time.time() - _link_cache[0] < 3: return _link_cache[1]
    try:
        out = subprocess.run(['tasklist', '/NH'], capture_output=True, text=True, creationflags=NOWIN, timeout=8).stdout.lower()
        _link_cache[1] = ('lld-link.exe' in out) or ('link.exe' in out.replace('lld-link.exe', ''))
    except Exception:
        pass
    _link_cache[0] = time.time()
    return _link_cache[1]


def build_state(logname=None):
    if logname is None: logname = selected_build()['log']
    lg = os.path.join(ROOT, 'analysis', logname)
    if logname == 'build_log.txt':      # PAL: the assistant's non-interactive build writes build_only_log.txt; use whichever log is newer
        alt = os.path.join(ROOT, 'analysis', 'build_only_log.txt')
        if os.path.exists(alt) and (not os.path.exists(lg) or os.path.getmtime(alt) > os.path.getmtime(lg)): lg = alt
    tail, sz = read_tail(lg, 65536)
    if not sz: return {'state': 'none'}
    mt = os.path.getmtime(lg)
    endm = re.findall(r'BUILD END[^\n]*exit=(-?\d+)', tail)
    done = 'BUILD STEP FINISHED' in tail or bool(endm)
    m = re.findall(r'\[(\d+)/(\d+)\]', tail)
    n, tot = (int(m[-1][0]), int(m[-1][1])) if m else (0, 0)
    bad = bool(re.search(r'(?<![A-Za-z-])error:', tail)) or 'FAILED:' in tail or (bool(endm) and endm[-1] != '0')
    running = (time.time() - mt) < 20 and not done
    linking = (not running) and (not done) and (not bad) and (time.time() - mt) < 3600 and (linker_running() or (n and tot and n >= tot - 8))
    if running: state = 'running'
    elif linking: state = 'linking'
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


def log_file(base, ver):
    # PAL keeps the original names; other versions use <base>_<id>.txt
    ver = ver if ver in BUILD_BY_ID else 'pal'
    return base + ('' if ver == 'pal' else '_' + ver) + '.txt'


def _last_run():
    p = os.path.join(ROOT, 'analysis', log_file('run_err', load_prefs().get('last_launched') or 'pal'))
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
    labels = {'ok': (True, 'finished'), 'failed': (False, 'FAILED - open Logs > Build'), 'interrupted': (None, 'interrupted or still running'), 'running': (None, 'running now'), 'linking': (None, 'linking now'), 'none': (None, 'no build log yet')}
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


def read_log(src, flt, ver='pal', tail_bytes=3_000_000, max_lines=1500):
    if src == 'build':
        bb = BUILD_BY_ID.get(ver) or BUILD_BY_ID['pal']
        path = os.path.join(ROOT, 'analysis', bb.get('log') or 'build_log.txt')
    else:
        path = os.path.join(ROOT, 'analysis', log_file('run_out' if src == 'run_out' else 'run_err', ver))
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
    {'id': 'ps2_ita', 'name': 'PlayStation 2 - Italy', 'region': 'PAL-I', 'sub': 'SLES-53677'},
    {'id': 'ps2_kor', 'name': 'PlayStation 2 - Korea', 'region': 'NTSC-K', 'sub': 'SLKA-25318'},
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
SERIAL_SLOT = {'SLES_536.76': 'ps2_pal', 'SLES_536.77': 'ps2_ita', 'SLUS_212.86': 'ps2_usa', 'SLPM_662.68': 'ps2_jpn', 'SLKA_253.18': 'ps2_kor'}
PS2_REGION_SLOT = {'SLKA': 'ps2_kor', 'SLES': 'ps2_pal', 'SCES': 'ps2_pal', 'SLUS': 'ps2_usa', 'SCUS': 'ps2_usa', 'SLPM': 'ps2_jpn', 'SLPS': 'ps2_jpn', 'SCPS': 'ps2_jpn', 'SCPM': 'ps2_jpn'}
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
    r.update(found=True, serial=ser, supported=(ser in ('SLES_536.76', 'SLUS_212.86')), slot=SERIAL_SLOT.get(ser) or PS2_REGION_SLOT.get(ser[:4]))
    r['note'] = (('Runs on the recompiled executable.' if ser == 'SLES_536.76' else 'Early build: boots, not usable yet.') if r['supported'] else
                 'Recognised and linked, but this region has no recompiled build yet (planned). The data can still be used for asset comparisons.')
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
            slot = SERIAL_SLOT.get(f.upper()) or (PS2_REGION_SLOT.get(m.group(1)) if m else None)
            if m and slot and slot not in auto: auto[slot] = dp
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
# Cheat addresses are PER GAME VERSION. A build only gets writes if its table below is verified; cheats are stored per build in
# prefs['cheats'][build_id] and written only for the build that is actually running (prefs['last_launched']).
CHEAT_MAPS = {
    'pal': {'verified': True, 'save_base': 0x3D9BF8, 'shop_table': 0x591FF8},
    # USA (CL-0168, mapped 2026-11-09, not yet tried in game): from 0x399780 on (end of .data, .bss) USA = PAL + 0x1380 (code references
    # to the save-buffer area match 1:1 at +0x1380; the old +0x1280 guess was wrong). Shop table: it is in the MENU overlay (PAL base
    # 0x4D2C00, USA 0x4D3F80, same image offset 0xBF3F8); all 137 entries are byte-identical in the USA overlay.
    'usa': {'verified': True, 'save_base': 0x3DAF78, 'shop_table': 0x593378},
}
SHOP_STRIDE, SHOP_COUNT = 0x28, 137
BITMAP_OFFS = (0x6A38C, 0x6A39E)           # owned-item bitmaps (S0 + mirror S1), bit n = shop table entry n
# 'Make NPCs playable' (CL-0169): roster records of the non-playable people, unlock field = 3. From the PAL GS2v3+/Xploder codes
# (Documents\PCSX2\cheats\SLES-53676_0B05A5BD.pnach: 0x3DA9C4 Vince McMahon ... 0x3DD624 Green Model), stored as offsets from
# save_base so the USA table (+0x1380 in this range) gets the same records. Record stride 0xA0.
NPC_UNLOCK_OFFS = [x - 0x3D9BF8 for x in (0x3DA9C4, 0x3DAA64, 0x3DBB44, 0x3DBBE4, 0x3DBC84, 0x3DBD24, 0x3DBDC4, 0x3DBE64, 0x3DBF04,
                                          0x3DD264, 0x3DD3A4, 0x3DD444, 0x3DD4E4, 0x3DD584, 0x3DD624)]
# CL-0171: three of those records have no name in the game data (empty strings): Bar Guy, Bar Maid, Green Model. A roster record
# (0xA0 bytes, starts 0x7E before the unlock byte) holds the full name at +0x00 and the short name at +0x5C (both 0-terminated).
NPC_NAMES = {0x3DD4E4 - 0x3D9BF8: 'Bar Guy', 0x3DD584 - 0x3D9BF8: 'Bar Maid', 0x3DD624 - 0x3D9BF8: 'Green Model'}


def poke_bytes(addr, data):
    """Console lines that write `data` at any (unaligned) address, keeping the neighbouring bytes of the first/last word."""
    lines = []
    a0 = addr & ~3; a1 = (addr + len(data) + 3) & ~3
    for w in range(a0, a1, 4):
        cur = peek32(w)
        if cur is None: return None
        bs = bytearray(cur.to_bytes(4, 'little'))
        for i in range(4):
            k = w + i - addr
            if 0 <= k < len(data): bs[i] = data[k]
        nv = int.from_bytes(bs, 'little')
        if nv != cur: lines.append('poke32 %x %x' % (w, nv))
    return lines


# CL-0172: GM money of the player's brand. PAL 0x430AC0 (= save_base + 0x56EC8), user-confirmed as his GM cash (value 1,624,144).
# 0x430B60 (+0xA0, 0 on this save) is probably the other brand's budget and is left alone.
GM_MONEY_OFF, GM_MONEY_MAX = 0x430AC0 - 0x3D9BF8, 9999999
_opentitles_on = False
_god_on = False
GM_MONEY_RAW_OFF = 0x42E4B0 - 0x3D9BF8   # RAW brand budget, signed 64-bit (high word must be 0 when positive); confirmed in game   # 9,999,999 fits the GM cash boxes (confirmed in game)
def cheat_map(bid): return CHEAT_MAPS.get(bid) or {'verified': False, 'save_base': None, 'shop_table': None}
def cheats_of(p, bid):
    c = p.get('cheats') or {}
    return c.get(bid) if isinstance(c.get(bid), dict) else {}

# ---------------------------------------------------------------- game settings file (shared with the F1 menu)
# The game keeps svr06_settings.json in the build folder and re-reads it when it changes, so the launcher and F1 menu are two views of one file.
# 'restart': needs a game restart; otherwise it applies live (cheats: 'live' = every few frames, 'menu' = re-enter the screen).
GSET = {
    'graphics.internalRes': {'restart': True, 'min': 0, 'max': 3}, 'graphics.renderer': {'restart': True, 'min': 0, 'max': 1},
    'graphics.aspect': {'restart': False, 'min': 0, 'max': 3}, 'graphics.filter': {'restart': False, 'min': 0, 'max': 1},
    'graphics.vsync': {'restart': False, 'min': 0, 'max': 1}, 'graphics.showFps': {'restart': False, 'min': 0, 'max': 1},
    'access.colorblind': {'restart': False, 'min': 0, 'max': 3}, 'access.colorblindStrength': {'restart': False, 'min': 0, 'max': 100},
    'menu.highContrast': {'restart': False, 'min': 0, 'max': 1}, 'menu.boldText': {'restart': False, 'min': 0, 'max': 1}, 'menu.reduceMotion': {'restart': False, 'min': 0, 'max': 1},
    'input.p1.mashAssist': {'restart': False, 'min': 0, 'max': 1}, 'input.p2.mashAssist': {'restart': False, 'min': 0, 'max': 1}, 'input.p1.sticky': {'restart': False, 'min': 0, 'max': 1},
    'audio.master': {'restart': False, 'min': 0, 'max': 100}, 'audio.mute': {'restart': False, 'min': 0, 'max': 1},
}
_GS_LAST = {}


def gs_path(bid):
    b = BUILD_BY_ID.get(bid) or BUILDS[0]
    return os.path.join(os.path.abspath(os.path.join(ROOT, '..', b['dir'], b['bdir'])), 'svr06_settings.json')


def gs_read(bid):
    try:
        with open(gs_path(bid), 'r', encoding='utf-8') as f: d = json.load(f)
        return {k: v for k, v in d.items() if isinstance(v, (int, float))}
    except Exception:
        return {}


def gs_write(bid, key, val):
    """Read-modify-write one key so the game's other settings are kept."""
    d = gs_read(bid)
    if d.get(key) == val: return
    d[key] = val
    pth = gs_path(bid)
    os.makedirs(os.path.dirname(pth), exist_ok=True)
    tmp = pth + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f: json.dump(d, f, indent=2)
    os.replace(tmp, pth)


CH_CHOICE = ['off', '1', '2', 'unlimited']


def gs_write_many(bid, kv):
    d = gs_read(bid); d.update(kv)
    pth = gs_path(bid)
    os.makedirs(os.path.dirname(pth), exist_ok=True)
    tmp = pth + '.tmp'
    with open(tmp, 'w', encoding='utf-8') as f: json.dump(d, f, indent=2)
    os.replace(tmp, pth)


def _launcher_values(p, bid):
    """Every launcher cheat / game option as {settings-file key: int} plus the default an absent key stands for."""
    vals, defs = {}, {}
    ch = cheats_of(p, bid)
    for c in CHEATS:
        cid = c['id']
        if c['kind'] == 'checks':
            sel = {int(n) for n in (ch.get(cid) or []) if str(n).isdigit()}
            for n, _nm in c['items']:
                k = 'cheat.%s.%d' % (cid, n); vals[k] = 1 if n in sel else 0; defs[k] = 0
        else:
            for w in (['p1', 'p2'] if c.get('per') else ['']):
                kk = cid + ('.' + w if w else ''); v = ch.get(kk)
                iv = (CH_CHOICE.index(v) if v in CH_CHOICE else 0) if c['kind'] == 'choice' else (1 if v else 0)
                vals['cheat.' + kk] = iv; defs['cheat.' + kk] = 0
    mids = [m['id'] for m in MODES]; iids = [i['id'] for i in ICON_SETS]
    vals['game.mode'] = mids.index(p.get('mode')) if p.get('mode') in mids else 0; defs['game.mode'] = 0
    vals['game.iconSet'] = iids.index(p.get('icons') or '') if (p.get('icons') or '') in iids else 0; defs['game.iconSet'] = 0
    vals['game.texpack'] = 1 if p.get('texpack') else 0; defs['game.texpack'] = 0
    vals['game.probes'] = 1 if p.get('probes', True) else 0; defs['game.probes'] = 1
    vals['game.skipMovies'] = 1 if p.get('skipmovies') else 0; defs['game.skipMovies'] = 0
    vals['game.arcade'] = 1 if p.get('arcade') else 0; defs['game.arcade'] = 0
    return vals, defs


def gs_sync_all(bid):
    """Launcher prefs <-> svr06_settings.json (the F1 menu's store) for every cheat and game option. Whoever changed last wins.
    An absent key means the default, so the file only grows when something is changed away from the default."""
    gs = gs_read(bid); p = load_prefs()
    vals, defs = _launcher_values(p, bid)
    to_file, to_prefs = {}, {}
    for k, lv in vals.items():
        gv = int(gs[k]) if k in gs else defs[k]
        last = _GS_LAST.get((bid, k))
        if last is None: cur = gv if k in gs else lv
        elif gv != last: cur = gv
        elif lv != last: cur = lv
        else: cur = last
        if cur != lv: to_prefs[k] = cur
        if cur != gv: to_file[k] = cur
        _GS_LAST[(bid, k)] = cur
    if to_file: gs_write_many(bid, to_file)
    if not to_prefs: return
    ch = dict(cheats_of(p, bid))
    mids = [m['id'] for m in MODES]; iids = [i['id'] for i in ICON_SETS]
    for k, v in to_prefs.items():
        if k.startswith('cheat.'):
            rest = k[6:]; m = re.match(r'^(.+)\.(\d+)$', rest)
            if m and CHEAT_BY_ID.get(m.group(1), {}).get('kind') == 'checks':
                sel = {int(n) for n in (ch.get(m.group(1)) or []) if str(n).isdigit()}
                (sel.add if v else sel.discard)(int(m.group(2))); ch[m.group(1)] = sorted(sel)
            elif CHEAT_BY_ID.get(rest.split('.')[0], {}).get('kind') == 'choice': ch[rest] = CH_CHOICE[max(0, min(3, v))]
            else: ch[rest] = bool(v)
        elif k == 'game.mode': p['mode'] = mids[max(0, min(len(mids) - 1, v))]
        elif k == 'game.iconSet': p['icons'] = iids[max(0, min(len(iids) - 1, v))]
        elif k == 'game.texpack': p['texpack'] = bool(v)
        elif k == 'game.probes': p['probes'] = bool(v)
        elif k == 'game.skipMovies': p['skipmovies'] = bool(v)
        elif k == 'game.arcade': p['arcade'] = bool(v)
    c = dict(p.get('cheats') or {}); c[bid] = ch; p['cheats'] = c; save_prefs(p)


# CL-0178: shop table entries (index = bitmap bit = shop table row) with names read from the game's text table (tools/gettext_dump.py, PAL)
SHOP_ITEMS = {"unlock_legend": [[4, "Andre The Giant"], [5, "Bret \"Hit Man\" Hart"], [6, "Mankind"], [8, "British Bulldog\"Davey Boy Smith\""], [13, "Junkyard Dog"], [7, "The Rock"], [9, "Jimmy Hart"], [10, "Stone Cold Steve Austin"], [11, "Hulk Hogan 80's"], [12, "Hollywood Hogan"], [14, "\"The Million Dollar Man\"Ted DiBiase"], [15, "Jake \"The Snake\" Roberts"]], "unlock_costume": [[19, "Steve Austin T-shirt and Jeans"], [20, "JBL Suit"], [21, "Triple H Suit"], [22, "John Cena Jersey"], [23, "Carlito T-Shirt"]], "unlock_loading": [[24, "Diva Loading Screens"], [25, "Legend Loading Screens"], [26, "Loading Screen Manager ECW Sets"], [27, "Loading Screen Sets 1"], [28, "Loading Screen Sets 2"]], "unlock_trophy": [[33, "the \"GM of the Year Trophy\""], [34, "the \"RAW Trophy\""], [35, "the \"SmackDown! Trophy\""], [36, "the \"Legend Killer Trophy\""], [37, "the \"Collect-A-Diva\""], [38, "the \"Undertaker's Urn\""], [39, "the \"Bar Brawl\""], [40, "the \"Royal Rumble\""], [41, "the \"Elimination Chamber\""], [42, "the \"Challenge Mode Amateur\""], [43, "the \"Challenge Mode Rising Star\""], [44, "the \"Challenge Mode Superstar\""], [45, "the \"Challenge Mode Legend\""], [46, "the \"Tough Enough Trophy\""], [47, "the \"100 Wins Trophy\""], [48, "the \"Match Veteran\""]], "unlock_deco": [[54, "Furniture Collection"], [55, "Locker Room Hardcore Wall Ornament Collection"], [56, "Locker Room Jersey Collection"], [57, "Locker Room Necklace Collection"], [58, "Locker Room Collectible Action Figures"], [59, "Locker Room Diva Posters"], [60, "Locker Room Legendary Posters"], [61, "Poster ECW Sets"], [62, "Poster SmackDown! Sets"], [63, "Poster RAW Sets"], [64, "Exercise Bike"], [65, "Furniture Exergym"], [66, "Car Collection Sets"], [67, "Bubblehead Sets"], [68, "The Rock Bobblehead"], [69, "Hulk Hogan Bobblehead"], [70, "Eddie Lowrider"], [71, "JBL Limousine"], [72, "Undertaker Motorbike"], [73, "Carlito Apple"], [74, "JBL $100 Bill"], [75, "Eugene Stuffed Animal"], [76, "Regal Brass Knuckles"], [77, "Jimmy Hart Megaphone"], [78, "Bret Hart Sunglasses"], [79, "British Bulldog's Bulldog"], [80, "Kane Mask"], [81, "Batista Figure"], [82, "John Cena Figure"], [83, "John Cena Lock Chain"], [84, "Kurt Angle Gold Medal"], [85, "Stacy Stand Cut-Out"], [86, "Trish Stand Cut-Out"], [87, "Lava Lamp"], [88, "Leather Couch"], [89, "Furry Couch"], [90, "Lowrider Couch"], [91, "Glass Coffee Table"], [92, "Casino Table"], [93, "Tiger Skin Rug"], [94, "Sledgehammer"], [95, "Barbie Weapon"], [96, "Guitar"], [97, "John Cena Football Jersey"], [98, "John Cena Baseball Jersey"], [99, "Y2J Hockey Jersey"], [100, "Rey Mysterio Basketball Jersey"], [101, "Rey Mysterio Baseball Jersey"], [102, "Shane McMahon Jersey"], [103, "ECW T-Shirt"], [104, "Torrie Poster 1"], [105, "Torrie Poster 2"], [106, "Trish Poster 1"], [107, "Trish Poster 2"], [108, "Trish Poster 3"], [109, "Stacy Poster 1"], [110, "Stacy Poster 2"], [111, "Christy Poster 1"], [112, "Christy Poster 2"], [113, "Lita Poster"], [114, "Michelle Poster"], [115, "Bret Hart Poster"], [116, "The Rock Poster"], [117, "Mankind Poster"], [118, "Austin Poster"], [119, "Hogan Poster"], [120, "D-X Poster"], [121, "Evolution Poster"], [122, "n.W.o. Poster"], [123, "Hall of Fame Poster"], [124, "Metal Locker"], [125, "Glass Locker"], [126, "Survivor Series Steel Chair"], [127, "SummerSlam Steel Chair"], [128, "RoyalRumble Steel Chair"], [129, "WrestleMania 21 Steel Chair"], [130, "Metal Trophy Case"], [131, "Glass Trophy Case"], [132, "Metal Belt Case"], [133, "Glass Belt Case"], [134, "Training Machine 1"], [135, "Training Machine 2"], [136, "Fancy Component Stereo Set"], [137, "Ceiling Fan"]], "unlock_arena": [[16, "ECW Arena"], [17, "WrestleMania 21 Arena"], [18, "WrestleMania IX Arena and Legend Cage"], [29, "World Heavyweight Championship (#29)"], [30, "WWE Championship (#30)"], [31, "The Million Dollar Championship (#31)"], [32, "Hardcore Championship (#32)"], [49, "Unidentified shop item (#49)"], [50, "Unidentified shop item (#50)"], [51, "Unidentified shop item (#51)"], [52, "Unidentified shop item (#52)"], [53, "Unidentified shop item (#53)"]]}
CHEATS = [
    {'g': 'Exhibition', 'id': 'exh_open_titles', 'name': 'Any gender can win any title (Exhibition)', 'desc': 'Divas can win the men\'s titles, men can win the women\'s title, and tag titles can be held by any mix of men and women (man+woman, man+man, woman+woman).', 'kind': 'switch', 'status': 'working'},
    {'g': 'Match', 'id': 'auto_reversal', 'name': 'Auto reversal', 'desc': 'Every incoming attack is reversed.', 'per': True, 'kind': 'switch', 'status': 'todo'},
    {'g': 'Match', 'id': 'god_mode', 'name': 'God mode (no damage)', 'desc': 'Your limbs never take damage (PAL).', 'per': True, 'kind': 'switch', 'status': 'untested'},
    {'g': 'Match', 'id': 'finisher_start', 'name': 'Finishers at match start', 'desc': 'Start every match with this many finishers stored.', 'per': True, 'kind': 'choice', 'opts': ['off', '1', '2', 'unlimited'], 'status': 'untested'},
    {'g': 'Match', 'id': 'finisher_always', 'name': 'Finishers at all times', 'desc': 'Keep the stored finisher count at this level.', 'per': True, 'kind': 'choice', 'opts': ['off', '1', '2', 'unlimited'], 'status': 'untested'},
    {'g': 'Match', 'id': 'rumble_meter', 'name': 'Instant-deplete elimination meter', 'desc': 'Royal Rumble: the elimination meter empties at once.', 'kind': 'switch', 'status': 'todo'},
    {'g': 'Locker Room', 'id': 'unlock_all', 'name': 'Unlock all', 'desc': 'Marks every shop item (characters, legends, costumes, decorations, trophies, loading screens) as owned.', 'kind': 'switch', 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'free_shop', 'name': 'Everything costs $0', 'desc': 'Sets every shop price to zero.', 'kind': 'switch', 'status': 'working'},
    {'g': 'Locker Room', 'id': 'infinite_money', 'name': 'Infinite money (cash value)', 'desc': 'Keeps your WWE Shop cash at $99,999,999.', 'kind': 'switch', 'status': 'working'},
    {'g': 'Locker Room', 'id': 'unlock_legend', 'name': 'Unlock legends and managers', 'desc': 'Tick each one you want unlocked: Andre, Bret Hart, Mankind, Bulldog, JYD, The Rock, Stone Cold, Jake The Snake, Hogan 80s / Hollywood, DiBiase, Jimmy Hart.', 'kind': 'checks', 'items': SHOP_ITEMS['unlock_legend'], 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'unlock_costume', 'name': 'Unlock costumes (alt attire)', 'desc': 'Tick each costume to unlock.', 'kind': 'checks', 'items': SHOP_ITEMS['unlock_costume'], 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'unlock_loading', 'name': 'Unlock loading screens', 'desc': 'Tick each loading screen set to unlock.', 'kind': 'checks', 'items': SHOP_ITEMS['unlock_loading'], 'status': 'working'},
    {'g': 'Locker Room', 'id': 'unlock_trophy', 'name': 'Unlock trophies', 'desc': 'Tick each trophy to unlock.', 'kind': 'checks', 'items': SHOP_ITEMS['unlock_trophy'], 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'unlock_deco', 'name': 'Unlock locker room decorations', 'desc': 'Collections, furniture, posters, jerseys, lockers and more - tick each one.', 'kind': 'checks', 'items': SHOP_ITEMS['unlock_deco'], 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'unlock_arena', 'name': 'Unlock arenas and championships', 'desc': 'ECW Arena, WrestleMania arenas, Million Dollar and Hardcore titles.', 'kind': 'checks', 'items': SHOP_ITEMS['unlock_arena'], 'status': 'untested'},
    {'g': 'Locker Room', 'id': 'unlock_npc', 'name': 'Make NPCs playable', 'desc': 'Referees, managers and other non-roster people selectable in matches. Missing portraits and renders get fixed later.', 'kind': 'switch', 'status': 'working'},
    {'g': 'GM Mode', 'id': 'gm_five_stars', 'name': 'Shows and matches always get 5 stars', 'desc': 'Every played match and the whole show get a 5-star rating.', 'kind': 'switch', 'status': 'working'},
    {'g': 'GM Mode', 'id': 'gm_money', 'name': 'Unlimited GM money (SmackDown!)', 'desc': 'Keeps the SmackDown! brand budget (Starting Cash) at $9,999,999.', 'kind': 'switch', 'status': 'working'},
    {'g': 'GM Mode', 'id': 'gm_money_raw', 'name': 'Unlimited GM money (Raw)', 'desc': 'Keeps the Raw brand budget (Starting Cash) at $9,999,999.', 'kind': 'switch', 'status': 'working'},
    {'g': 'GM Mode', 'id': 'gm_open_titles', 'name': 'Any gender can win any title (GM Mode)', 'desc': 'Divas can win the men\'s titles, men can win the women\'s title, and tag titles can be held by any mix of men and women (man+woman, man+man, woman+woman).', 'kind': 'switch', 'status': 'untested'},
    {'g': 'GM Mode', 'id': 'gm_trades', 'name': 'Trades always accepted', 'desc': 'Not when another human is in control (single player against the computer only).', 'kind': 'switch', 'status': 'todo'},
    {'g': 'GM Mode', 'id': 'gm_no_fatigue', 'name': 'Superstars cannot get fatigue or injuries', 'desc': 'Clears fatigue and injuries for every superstar (both brands).', 'kind': 'switch', 'status': 'working'},
    {'g': 'Season Mode', 'id': 'season_win_skip', 'name': 'Always win when skipping a match', 'desc': '', 'kind': 'switch', 'status': 'todo'},
    {'g': 'General', 'id': 'savestates', 'name': 'Save / load states', 'desc': 'Needs support inside the game runtime, not a memory poke.', 'kind': 'switch', 'status': 'todo'},
]
CHEAT_BY_ID = {c['id']: c for c in CHEATS}
_CH = {'applied': {}, 'msg': '', 'last': 0.0, 'lock': threading.Lock()}


def console(lines, wait=3.0):
    """Send console lines to the running game and return what it printed."""
    if not svrlive: return None
    cur_live()
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


def apply_cheats(p, bid):
    """Write the enabled cheats of build `bid` into the running game. Returns a short status text."""
    cm = cheat_map(bid)
    if not cm['verified'] or not cm['save_base'] or not cm['shop_table']:
        return 'cheats are not mapped for %s yet - nothing written' % bid.upper()
    SAVE_BASE, SHOP_TABLE = cm['save_base'], cm['shop_table']
    ch = cheats_of(p, bid)
    lines = []
    if ch.get('unlock_all'):
        for off in BITMAP_OFFS:
            a = SAVE_BASE + off
            lines += ['poke32 %x fffffff8' % a, 'poke32 %x ffffffff' % (a + 4), 'poke32 %x ffffffff' % (a + 8), 'poke32 %x ffffffff' % (a + 12)]
            w = peek32(a + 16)
            if w is not None: lines.append('poke32 %x %x' % (a + 16, (w & 0xFFFF0000) | 0x01FF))   # CL-0170: a failed read no longer cancels the other cheats
    if ch.get('unlock_all') or ch.get('free_shop'):
        for i in range(3, SHOP_COUNT):
            b = SHOP_TABLE + i * SHOP_STRIDE
            if ch.get('unlock_all'): lines += ['poke32 %x 1' % (b + 7 * 4), 'poke32 %x 0' % (b + 9 * 4)]
            if ch.get('free_shop'): lines.append('poke32 %x 0' % (b + 8 * 4))
    picks = sorted({int(n) for k in SHOP_ITEMS for n in (ch.get(k) or []) if isinstance(n, int) or str(n).isdigit()})   # CL-0178: single-item unlocks
    if picks and not ch.get('unlock_all'):
        for off in BITMAP_OFFS:       # byte-exact: the mirror bitmap starts 2 bytes off a word boundary
            words = {}
            for n in picks:
                ba = SAVE_BASE + off + n // 8
                wa = ba & ~3
                words[wa] = words.get(wa, 0) | (1 << (((ba & 3) * 8) + n % 8))
            for wa, bits in sorted(words.items()):
                w = peek32(wa)
                if w is not None: lines.append('poke32 %x %x' % (wa, w | bits))
        for n in picks:
            b = SHOP_TABLE + n * SHOP_STRIDE
            lines += ['poke32 %x 1' % (b + 7 * 4), 'poke32 %x 0' % (b + 9 * 4)]
    if ch.get('unlock_npc'):
        # the unlock is the low BYTE of the word (GS2 code type 0 = 8-bit write); the upper bytes (0x00FFFFxx) are kept
        for off in NPC_UNLOCK_OFFS:
            w = peek32(SAVE_BASE + off)
            if w is not None and w & 0xFF != 3: lines.append('poke32 %x %x' % (SAVE_BASE + off, (w & 0xFFFFFF00) | 3))
        for off, nm in NPC_NAMES.items():
            rec = SAVE_BASE + off - 0x7E
            for f in (rec, rec + 0x5C):
                first = peek32(f & ~3)
                if first is None or (first >> ((f & 3) * 8)) & 0xFF: continue      # only fill an EMPTY name (never overwrite one)
                l = poke_bytes(f, nm.encode('ascii') + b'\0')
                if l: lines += l
    if ch.get('infinite_money'):
        lines.append('poke32 %x 5F5E0FF' % (SAVE_BASE + 0x6DD6C))
    if ch.get('gm_money'):
        lines.append('poke32 %x %x' % (SAVE_BASE + GM_MONEY_OFF, GM_MONEY_MAX))

    if ch.get('gm_money_raw'):
        lines.append('poke32 %x %x' % (SAVE_BASE + GM_MONEY_RAW_OFF, GM_MONEY_MAX))
        lines.append('poke32 %x 0' % (SAVE_BASE + GM_MONEY_RAW_OFF + 4))
    global _opentitles_on
    if ch.get('exh_open_titles'):
        lines.append('diag opentitles on'); _opentitles_on = True
    elif _opentitles_on:
        lines.append('diag opentitles off'); _opentitles_on = False
    lines.append('diag opentitles_gm %s' % ('on' if ch.get('gm_open_titles') else 'off'))
    for cid, nm1, nm2 in (('finisher_start', 'fin_start1', 'fin_start2'), ('finisher_always', 'fin_all1', 'fin_all2')):
        v = str(ch.get(cid) or 'off')       # CL-0176: stored-finisher count hook (PAL), diag bits 12-15
        lines.append('diag %s %s' % (nm1, 'on' if v == '1' else 'off'))
        lines.append('diag %s %s' % (nm2, 'on' if v in ('2', 'unlimited') else 'off'))
    global _god_on
    if ch.get('god_mode'):
        lines.append('diag godmode on'); _god_on = True
    elif _god_on:
        lines.append('diag godmode off'); _god_on = False
    if not lines: return 'nothing to apply'
    for i in range(0, len(lines), 60): console(lines[i:i + 60], 2.0)
    return 'applied %d writes' % len(lines)


def cheat_loop():
    """Cheats are applied by the game itself (F1 menu + svr06_settings.json). This loop only keeps the launcher and the menu in sync."""
    while True:
        time.sleep(1.0)
        try: gs_sync_all(selected_build()['id'])
        except Exception as e: _CH['msg'] = 'sync error: ' + str(e)[:100]
        else: _CH['msg'] = 'The game applies cheats itself; this page and the in-game F1 menu share the same settings.'



# ---------------------------------------------------------------- GitHub tickets
import urllib.request, urllib.error
TOKEN_FILE = os.path.join(ROOT, 'config', 'github_token.txt')


def gh_token():
    t = os.environ.get('SVR06_GITHUB_TOKEN', '').strip()
    if t: return t
    try:
        with open(TOKEN_FILE, 'r', encoding='utf-8') as f:
            t = f.read().strip()
            if t: return t
    except OSError: pass
    try:                                           # fall back to the GitHub CLI login if one exists
        r = subprocess.run(['gh', 'auth', 'token'], capture_output=True, text=True, timeout=8, creationflags=NOWIN if IS_WIN else 0)
        if r.returncode == 0 and r.stdout.strip(): return r.stdout.strip()
    except Exception: pass
    return ''


def gh(method, path, body=None):
    tok = gh_token()
    if not tok: raise RuntimeError('No GitHub token yet: Settings > GitHub, or sign in with the GitHub CLI (gh auth login).')
    repo = load_prefs().get('gh_repo') or 'jfussell94/svr06-recomp'
    req = urllib.request.Request('https://api.github.com/repos/%s%s' % (repo, path), method=method,
                                 data=json.dumps(body).encode('utf-8') if body is not None else None,
                                 headers={'Authorization': 'Bearer ' + tok, 'Accept': 'application/vnd.github+json', 'User-Agent': 'svr06-launcher',
                                          'X-GitHub-Api-Version': '2022-11-28', 'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(req, timeout=20) as r:
            raw = r.read(); return json.loads(raw) if raw else {}
    except urllib.error.HTTPError as e:
        msg = ''
        try: msg = json.loads(e.read().decode('utf-8', 'replace')).get('message', '')
        except Exception: pass
        raise RuntimeError('GitHub said %d %s' % (e.code, msg))


def gh_status():
    tok = gh_token()
    return {'has_token': bool(tok), 'repo': load_prefs().get('gh_repo'), 'source': 'env' if os.environ.get('SVR06_GITHUB_TOKEN') else 'file' if os.path.exists(TOKEN_FILE) else 'gh cli' if tok else ''}


def ticket_context(attach):
    p = load_prefs(); b = selected_build(p)
    lines = ['**Launcher version:** %s' % current_version(), '**Selected build:** %s (%s)' % (b['name'], b['serial']), '**Platform:** %s' % sys.platform]
    lr = last_run()
    if lr: lines.append('**Last run:** %s ago, %d suspicious log lines, icon set %s' % (fmt_age(time.time() - lr['mtime']), lr['errors'], lr.get('icon_set') or 'original'))
    ex = exe_path(b)
    if os.path.exists(ex): lines.append('**Executable built:** ' + time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(ex))))
    body = '\n'.join(lines)
    if attach:
        tail, _ = read_tail(os.path.join(ROOT, 'analysis', 'run_err.txt'), 24000)
        noise = re.compile(r'\[(spu2\] state|fps60|prof\] top|gsvk\] regs|sema\] spin)|FILEIO')
        keep = [l for l in (tail or '').splitlines() if not noise.search(l)][-80:]
        if keep: body += '\n\n<details><summary>run_err.txt (last lines, filtered)</summary>\n\n```text\n' + '\n'.join(l[:300] for l in keep)[-12000:] + '\n```\n</details>'
    return body


def ticket_create(d):
    title = (d.get('title') or '').strip()[:140]
    if not title: raise RuntimeError('Give the ticket a short title.')
    region = d.get('region') or selected_build()['id']; area = d.get('area') or 'other'
    body = '### What happened\n%s\n\n### Steps to reproduce\n%s\n\n### Details\n%s\n\n_Opened from the SvR06 launcher._' % (
        (d.get('what') or '(none)').strip(), (d.get('steps') or '(none)').strip(), ticket_context(bool(d.get('logs', True))))
    r = gh('POST', '/issues', {'title': title, 'body': body, 'labels': ['from-launcher', 'needs-triage', 'region:' + region, 'area:' + area]})
    p = load_prefs(); t = list(p.get('tickets') or [])
    t.insert(0, {'number': r['number'], 'title': title, 'url': r['html_url'], 'created': int(time.time())}); p['tickets'] = t[:100]; save_prefs(p)
    return {'number': r['number'], 'url': r['html_url']}


def tickets_list():
    mine = {t['number'] for t in (load_prefs().get('tickets') or [])}
    out = []
    for st in ('open', 'closed'):
        for i in gh('GET', '/issues?state=%s&labels=from-launcher&per_page=40&sort=updated' % st):
            if 'pull_request' in i: continue
            labs = [l['name'] for l in i.get('labels', [])]
            out.append({'number': i['number'], 'title': i['title'], 'url': i['html_url'], 'state': i['state'], 'labels': labs, 'updated': i['updated_at'],
                        'comments': i.get('comments', 0), 'verify': 'fixed-awaiting-confirmation' in labs and i['state'] == 'open', 'mine': i['number'] in mine})
    out.sort(key=lambda x: x['updated'], reverse=True)
    return out[:60]


def ticket_action(d):
    n = int(d.get('number'))
    act = d.get('action')
    if act == 'confirm':
        gh('POST', '/issues/%d/comments' % n, {'body': 'Confirmed fixed by the reporter in the launcher (%s). Closing.' % current_version()})
        try: gh('DELETE', '/issues/%d/labels/fixed-awaiting-confirmation' % n)
        except Exception: pass
        gh('PATCH', '/issues/%d' % n, {'state': 'closed', 'state_reason': 'completed'}); return 'Ticket #%d closed.' % n
    if act == 'reopen':
        gh('POST', '/issues/%d/comments' % n, {'body': 'Still broken in %s. %s' % (current_version(), (d.get('note') or '').strip())})
        try: gh('DELETE', '/issues/%d/labels/fixed-awaiting-confirmation' % n)
        except Exception: pass
        gh('POST', '/issues/%d/labels' % n, {'labels': ['still-broken']})
        gh('PATCH', '/issues/%d' % n, {'state': 'open'}); return 'Ticket #%d reopened.' % n
    if act == 'comment':
        gh('POST', '/issues/%d/comments' % n, {'body': (d.get('note') or '').strip()[:4000]}); return 'Comment added.'
    raise RuntimeError('unknown action')


def tickets_op(d):
    op = d.get('op')
    try:
        if op == 'status': return gh_status()
        if op == 'save_token':
            tok = (d.get('token') or '').strip()
            os.makedirs(os.path.dirname(TOKEN_FILE), exist_ok=True)
            with open(TOKEN_FILE, 'w', encoding='utf-8') as f: f.write(tok)
            return dict(gh_status(), message='Token saved locally (config/github_token.txt, never uploaded).')
        if op == 'set_repo':
            p = load_prefs(); p['gh_repo'] = (d.get('repo') or '').strip(); save_prefs(p); return gh_status()
        if op == 'test':
            r = gh('GET', ''); return {'message': 'Connected to ' + r.get('full_name', '?') + (' (can open tickets)' if r.get('permissions', {}).get('push') or r.get('has_issues') else '')}
        if op == 'create': return dict(ticket_create(d), message='Ticket opened.')
        if op == 'list': return {'tickets': tickets_list()}
        if op == 'action': return {'message': ticket_action(d), 'tickets': tickets_list()}
    except Exception as e:
        return {'error': str(e)}
    return {'error': 'unknown op'}



# ---------------------------------------------------------------- memory cards
# The runtime always reads <boot file folder>/mc0. Named cards live in <boot file folder>/cards/<name>; mc0 is a directory junction
# (no admin needed) pointing at the active card, so switching cards only re-points the junction. Nothing is ever deleted.
def _card_base(b):
    elf = find_named(b['elf'])
    return os.path.dirname(elf) if elf else ''


def _is_link(path):
    try: return os.path.exists(path) and os.path.normcase(os.path.realpath(path)) != os.path.normcase(os.path.abspath(path))
    except Exception: return False


def _junction(link, target):
    if os.path.lexists(link):
        if _is_link(link): os.rmdir(link)
        else: raise RuntimeError('mc0 is a real folder')
    r = subprocess.run(['cmd', '/c', 'mklink', '/J', link, target], capture_output=True, text=True)
    if r.returncode != 0: raise RuntimeError((r.stderr or r.stdout or 'mklink failed').strip())


def cards_ensure(b):
    base = _card_base(b)
    if not base: return None
    cd = os.path.join(base, 'cards'); os.makedirs(cd, exist_ok=True)
    mc0 = os.path.join(base, 'mc0')
    if not _is_link(mc0):
        dflt = os.path.join(cd, 'Default')
        if os.path.isdir(mc0):
            if os.path.exists(dflt): dflt = os.path.join(cd, 'Card ' + time.strftime('%Y%m%d_%H%M%S'))
            os.rename(mc0, dflt)
        else:
            os.makedirs(dflt, exist_ok=True)
        _junction(mc0, dflt)
    for n in os.listdir(base):          # old mc0_backup_<time> folders become selectable cards
        if n.startswith('mc0_backup_') and os.path.isdir(os.path.join(base, n)) and not os.path.exists(os.path.join(cd, 'Backup ' + n[11:])):
            os.rename(os.path.join(base, n), os.path.join(cd, 'Backup ' + n[11:]))
    return base


_CI = [0.0, None, None]


def cards_info(b):
    if _CI[1] is not None and _CI[2] == b['id'] and time.time() - _CI[0] < 5: return _CI[1]
    r = _cards_info(b); _CI[0] = time.time(); _CI[1] = r; _CI[2] = b['id']
    return r


def _cards_info(b):
    try:
        base = _card_base(b)
        if not base: return {'items': [], 'current': '', 'ready': False}
        cd = os.path.join(base, 'cards'); mc0 = os.path.join(base, 'mc0')
        names = sorted(n for n in os.listdir(cd) if os.path.isdir(os.path.join(cd, n))) if os.path.isdir(cd) else []
        cur = os.path.basename(os.path.realpath(mc0)) if _is_link(mc0) else ('Default' if os.path.isdir(mc0) else '')
        def sz(n):
            try: return sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(os.path.join(cd, n)) for f in fs)
            except Exception: return 0
        if not _is_link(mc0) and os.path.isdir(mc0): names = ['Default'] + [n for n in names if n != 'Default']
        return {'items': [{'name': n, 'kb': round(sz(n) / 1024)} for n in names], 'current': cur, 'ready': _is_link(mc0), 'build': b['id']}
    except Exception as e:
        return {'items': [], 'current': '', 'ready': False, 'error': str(e)}


def card_select(name, create=False):
    hb = heartbeat()
    if hb and hb['alive']: return 'Close the game first, then change the memory card.'
    b = selected_build()
    base = cards_ensure(b)
    if not base: return 'Game files for %s are not linked yet.' % b['name']
    cd = os.path.join(base, 'cards')
    name = re.sub(r'[\\/:*?"<>|]', '', (name or '')).strip()
    if create:
        name = name or ('Blank ' + time.strftime('%Y%m%d_%H%M%S'))
        if os.path.exists(os.path.join(cd, name)): return 'A card called "%s" already exists.' % name
        os.makedirs(os.path.join(cd, name))
    if not name or not os.path.isdir(os.path.join(cd, name)): return 'No such memory card.'
    _junction(os.path.join(base, 'mc0'), os.path.join(cd, name))
    p = load_prefs(); p.setdefault('cards', {})[b['id']] = name; save_prefs(p); _CI[1] = None
    return ('New blank memory card "%s" created and selected' if create else 'Memory card "%s" selected') % name + ' for ' + b['name'] + '.'


def card_prepare_for_launch(b):
    # re-apply the card chosen for this version before the game starts (cheap no-op when already selected)
    try:
        want = (load_prefs().get('cards') or {}).get(b['id'])
        base = cards_ensure(b)
        if base and want and os.path.isdir(os.path.join(base, 'cards', want)):
            mc0 = os.path.join(base, 'mc0')
            if os.path.basename(os.path.realpath(mc0)) != want: _junction(mc0, os.path.join(base, 'cards', want))
    except Exception:
        pass

# ---------------------------------------------------------------- actions
def spawn(cmd):
    p = load_prefs()
    subprocess.Popen(cmd, shell=True, cwd=ROOT, env=build_env(p))


def do_action(name, arg, body):
    if name == 'gset':
        k = body.get('key'); m = GSET.get(k)
        if not m: return 'unknown setting'
        v = max(m['min'], min(m['max'], int(body.get('value', 0))))
        bid = selected_build()['id']; gs_write(bid, k, v)
        return 'Saved' + (' - restart the game to apply' if m['restart'] else (' - applies in the running game' if game_running_cached() else ''))
    sc = os.path.join(ROOT, 'scripts')
    if name in ('play', 'rebuild', 'build'):
        if arg in BUILD_BY_ID:
            set_selected_build(arg)
        b = selected_build()
        if name in ('play', 'rebuild'): card_prepare_for_launch(b)
        if name == 'play':
            if not os.path.exists(build_exe(b)): return b['name'] + ' is not built yet - use Rebuild + play.'
            pp = load_prefs(); pp['last_launched'] = b['id']; save_prefs(pp)
            spawn('"%s"' % os.path.join(sc, 'run.cmd')); return 'Starting ' + b['name'] + '...'
        if name == 'rebuild':
            pp = load_prefs(); pp['last_launched'] = b['id']; save_prefs(pp)
            spawn('"%s"%s' % (os.path.join(sc, b['rebuild']), ' -Launch' if b['id'] != 'pal' else '')); return 'Rebuilding ' + b['name'] + ', the game starts when it is done.'
        spawn('"%s"' % os.path.join(sc, b['build'])); return 'Building ' + b['name'] + ' only (no launch).'
    if name == 'newcard':
        return card_select(arg, create=True)
    if name == 'cardsel':
        return card_select(arg)
    if name == 'quit':
        r = None
        if svrlive:
            try: r = svrlive.send(['quit'], 6)
            except Exception: r = None
        if IS_WIN: subprocess.Popen('taskkill /IM %s%s' % (exe_of(load_prefs().get('last_launched') or 'pal'), ' & taskkill /IM svr06run.exe' if (load_prefs().get('last_launched') or 'pal') == 'pal' else ''), shell=True, creationflags=NOWIN)
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
        fs = sorted(f for f in os.listdir(d) if f.startswith('prev_') and re.search(r'_run_(err|out)(_[a-z0-9]+)?\.txt$', f))
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
    try: gs_sync_all(selected_build()['id'])
    except Exception: pass
    p = load_prefs()
    lr = last_run()
    ex = exe_path()
    return {
        'now': time.time(), 'version': current_version(), 'prefs': p, 'env': env_summary(p),
        'modes': MODES, 'icon_sets': [dict(s, ready=(not s['id']) or os.path.exists(os.path.join(ROOT, 'mods', 'icons', s['id'], 'glyph_idx.bin'))) for s in ICON_SETS],
        'texpack': {'count': count_files(TEXPACK, '.png'), 'path': TEXPACK_REL, 'stage': 'detect'},
        'builds': builds_info(), 'cards': cards_info(selected_build(p)), 'states': STATES, 'sel_build': selected_build(p)['id'],
        'game': {'running': game_running_cached(), 'heartbeat': heartbeat(), 'exe_mtime': os.path.getmtime(ex) if os.path.exists(ex) else 0},
        'build': build_state(), 'last_run': lr, 'scenarios': sorted(os.listdir(os.path.join(TOOLS, 'scenarios'))) if os.path.isdir(os.path.join(TOOLS, 'scenarios')) else [],
        'gset': gs_read(selected_build(p)['id']), 'gset_meta': GSET, 'cheats': {'defs': CHEATS, 'msg': _CH['msg'], 'verified': {k: v['verified'] and bool(v['shop_table']) for k, v in CHEAT_MAPS.items()}}, 'library': library(), 'root': ROOT, 'platform': 'windows' if IS_WIN else sys.platform,
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
            if path == '/api/log': return self._send(200, read_log((q.get('src') or ['run_err'])[0], (q.get('f') or [''])[0], (q.get('ver') or ['pal'])[0]))
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
                p = load_prefs()
                if 'build' in body and body['build'] != p.get('build'):
                    set_selected_build(body['build']); p = load_prefs()            # switch version first, then apply the rest to that version
                p.update({k: v for k, v in body.items() if k in DEFAULT_PREFS and k not in ('build', 'per_build')})
                save_prefs(p)
                try: gs_sync_all(selected_build(p)['id'])
                except Exception: pass
                p = load_prefs()
                return self._send(200, {'prefs': p, 'env': env_summary(p)})
            if u.path == '/api/action':
                return self._send(200, {'message': do_action(body.get('name', ''), body.get('arg', ''), body)})
            if u.path == '/api/games':
                r = games_op(body); r['env'] = env_summary(load_prefs()); return self._send(200, r)
            if u.path == '/api/tickets':
                return self._send(200, tickets_op(body))
            if u.path == '/api/exit':
                threading.Thread(target=lambda: (time.sleep(0.2), os._exit(0)), daemon=True).start()
                return self._send(200, {'ok': True})
            return self._send(404, {'error': 'unknown'})
        except Exception as e:
            return self._send(500, {'error': str(e)})


def make_server(port=0):
    threading.Thread(target=cheat_loop, daemon=True).start()
    return ThreadingHTTPServer(('127.0.0.1', port), Handler)
