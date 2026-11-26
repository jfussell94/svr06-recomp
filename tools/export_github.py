#!/usr/bin/env python3
"""Builds a clean, GitHub-ready copy of the project (original work only; no game data, no code generated from the game).
usage: python tools/export_github.py <out_dir> [--old-repo <existing git checkout>]
Writes <out_dir>/ and DELETED_FROM_REPO.txt (files in the old repo that no longer exist). Safe to re-run (output is rebuilt)."""
import os, re, sys, shutil, subprocess, json, time
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
RT_SRC = os.path.abspath(os.environ.get('SVR06_RT_SRC') or os.path.join(ROOT, '..', 'PS2Recomp'))  # SVR06_RT_SRC=..\PS2Recomp_usa vendors the USA runtime (CL-0396)
out = os.path.abspath(sys.argv[1]); old = None
if '--old-repo' in sys.argv: old = os.path.abspath(sys.argv[sys.argv.index('--old-repo') + 1])
os.makedirs(out, exist_ok=True)   # files are overwritten in place (a connected-folder shell cannot delete)
copied = []
def put(src, rel):
    dst = os.path.join(out, rel); os.makedirs(os.path.dirname(dst), exist_ok=True); shutil.copyfile(src, dst); copied.append(rel.replace('\\', '/'))
def text(rel, s):
    dst = os.path.join(out, rel); os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='utf-8', newline='\n').write(s); copied.append(rel.replace('\\', '/'))
BAD = re.compile(r'(\.bak|\.cl\d+|\.orig|~$|__pycache__|\.pyc$)')
def walk(base, exts=None, skipdirs=(), maxsize=1500000, skipnames=lambda n: False):
    for dp, dn, fn in os.walk(base):
        dn[:] = [d for d in dn if d not in skipdirs and not BAD.search(d) and not d.startswith('build')]
        for f in fn:
            p = os.path.join(dp, f)
            if BAD.search(f) or skipnames(f): continue
            if exts and os.path.splitext(f)[1].lower() not in exts: continue
            try:
                if os.path.getsize(p) > maxsize: continue
            except OSError: continue
            yield p, os.path.relpath(p, base)
# ---- original code
# CL-0271: never publish game-derived VU1 data (tools/vu1bench/img, natlist.txt) or the native programs generated from it
NEVER = ('ps2_vu1_native_gen.cpp', 'natlist.txt', 'native_gen.cpp', 'empty_gen.cpp')
for p, r in walk(os.path.join(ROOT, 'tools'), {'.py', '.sh', '.ps1', '.cmd', '.md', '.txt', '.js', '.css', '.html', '.cpp', '.h', '.woff2'}, skipdirs=('__pycache__', 'codeaudit', 'img', '_to_delete'),
                 skipnames=lambda n: n in ('progress.json', 'game_completion.json') or n in NEVER):
    put(p, 'tools/' + r)
# CL-0271: launcher artwork (original work): app icon, launcher icon, font licence
for rel in ('tools/launcher/ui/icon.png', 'tools/launcher/ui/fonts/OFL-Archivo.txt'):
    if os.path.exists(os.path.join(ROOT, rel)): put(os.path.join(ROOT, rel), rel)
for p, r in walk(os.path.join(ROOT, 'art'), {'.png', '.ico'}, maxsize=3000000):
    put(p, 'art/' + r)
if os.path.exists(os.path.join(ROOT, 'docs', 'images', 'logo.png')): put(os.path.join(ROOT, 'docs', 'images', 'logo.png'), 'docs/images/logo.png')
for p, r in walk(os.path.join(ROOT, 'scripts'), {'.ps1', '.cmd', '.sh', '.py'}, skipnames=lambda n: n.startswith('_') or n in ('probe_env.cmd',)):
    put(p, 'scripts/' + r)
NOGAMEDOC = re.compile(r'(FILE_LOOKUP|GAME_FILE_INVENTORY|DPAC_TOC|MUSIC_LIST_MAP|COLLISION_MAP|PUBLISH_code-audit)', re.I)
for p, r in walk(os.path.join(ROOT, 'docs'), {'.md'}, skipnames=lambda n: bool(NOGAMEDOC.search(n)) or re.match(r'CHANGELOG-\d', n) is not None and False):
    put(p, 'docs/' + r)
put(os.path.join(ROOT, 'config', 'hle_plan.md'), 'config/hle_plan.md')
# hand-written runtime hooks only (everything generated from the game is excluded)
HAND = ['svr06_engine.h', 'svr06_funcs.h', 'svr06_hot_api.h', 'svr06_hotload.cpp', 'svr06_overlay.cpp', 'svr06_overlay.h', 'svr06_overrides.cpp']
for f in HAND:
    s = os.path.join(ROOT, 'overrides', f)
    if os.path.exists(s): put(s, 'overrides/' + f)
USA_RUN = os.path.abspath(os.path.join(ROOT, '..', 'PS2Recomp_usa', 'ps2xRuntime', 'src', 'runner'))  # CL-0396: USA hand-edited hooks (USA addresses)
for f in ('svr06_overrides.cpp', 'svr06_funcs.h', 'svr06_hotload.cpp', 'svr06_overlay.cpp'):
    s = os.path.join(USA_RUN, f)
    if os.path.exists(s): put(s, 'overrides/usa/' + f)
text('overrides/README.md', 'Hand-written runtime hooks only. `overrides/usa/` holds the USA (SLUS-21286) hooks with USA addresses; the files next to this README are the PAL reference. Overlay banks (ovl*/ovx*), recompiled functions (sub_*/entry_*) and *_functions.h are generated from your own disc and are git-ignored.\n')
# ---- regions
REG = json.load(open(os.path.join(ROOT, 'regions', 'regions.json')))
put(os.path.join(ROOT, 'regions', 'regions.json'), 'regions/regions.json')
put(os.path.join(ROOT, 'regions', 'regions.json'), 'release/regions.json')   # CL-0271: README and packaging refer to release/regions.json
for r in REG['regions']:
    rid = r['id']
    src = os.path.join(ROOT, r['recomp_config'])
    if os.path.exists(src): put(src, 'regions/%s/recomp.toml' % rid)
    text('regions/%s/region.json' % rid, json.dumps(r, indent=2) + '\n')
    rd = os.path.join(ROOT, 'regions', rid, 'README.md')
    if os.path.exists(rd): put(rd, 'regions/%s/README.md' % rid)
    else: text('regions/%s/README.md' % rid, '# %s - %s\n\nBoot file: `%s` (you supply it from your own disc; never committed).\nStatus: %s.\n\nThis folder holds only addresses and options for the recompiler. Everything generated from the game stays on your machine.\n' % (r['name'], r['serial'], r['boot_elf'], r['status']))
text('regions/README.md', '''# Regions

One repository supports every release of the game. A region is a folder here:

    regions/<id>/region.json   id, serial, boot file name, status, local output folders
    regions/<id>/recomp.toml   recompiler configuration (addresses/options only)
    regions/<id>/README.md

**No forks are needed.** To add a region: copy an existing folder, run `python tools/usa_map.py`-style address mapping (or the analyzer) against the new boot file,
add it to `release/regions.json` and the launcher's `BUILDS` list (`tools/launcher/server.py`).
Shared code (runtime, launcher, tools) is region independent; region-specific hooks live behind the address maps in each region folder.
''')
# ---- vendored PS2Recomp (upstream + our runtime changes)
GEN = re.compile(r'^(sub_|entry_|ovl[A-Za-z]|ovx|ps2_recompiled_|register_functions)|_functions\.h$')
def rt_ok(f): return not GEN.search(f) and f != 'ps2_vu1_native_gen.cpp'   # CL-0271: generated from the game's VU1 microcode, never published
oldlist = []
if old:
    try: oldlist = subprocess.run(['git', 'ls-files'], cwd=old, capture_output=True, text=True, timeout=60).stdout.split('\n')
    except Exception: oldlist = []
oldrt = [x for x in oldlist if x.startswith('PS2Recomp/')]
seen = set()
for x in oldrt:
    s = os.path.join(RT_SRC, x[len('PS2Recomp/'):])
    if os.path.isfile(s) and rt_ok(os.path.basename(s)) and not BAD.search(x): put(s, x); seen.add(x)
for sub in ('ps2xRuntime/src/lib', 'ps2xRuntime/include', 'ps2xRuntime/cmake', 'ps2xIOP/src', 'ps2xIOP/include', 'ps2xRecomp/src', 'ps2xRecomp/include'):   # CL-0402: the recompiler (incl. src/runner/main.cpp) is needed to build from a fresh checkout
    for p, r in walk(os.path.join(RT_SRC, sub), None, maxsize=3000000, skipnames=lambda n: not rt_ok(n)):
        rel = 'PS2Recomp/' + sub + '/' + r.replace('\\', '/')
        if rel not in seen: put(p, rel); seen.add(rel)
for f in ('ps2xRuntime/CMakeLists.txt', 'ps2xIOP/CMakeLists.txt'):
    rel = 'PS2Recomp/' + f
    if rel not in seen and os.path.exists(os.path.join(RT_SRC, f)): put(os.path.join(RT_SRC, f), rel)
# CL-0404: this project's changes to paraLLEl-GS/Granite (LGPL), laid over the pinned upstream clone by scripts/usa_build.ps1
for p, r in walk(os.path.join(ROOT, 'patches'), None, maxsize=3000000):
    put(p, 'patches/' + r.replace('\\', '/'))
# ---- repo scaffolding
shutil.copyfile(os.path.join(ROOT, 'tools', 'github_templates', '.gitignore'), os.path.join(out, '.gitignore')) if os.path.exists(os.path.join(ROOT, 'tools', 'github_templates', '.gitignore')) else None
for f in ('.gitignore', '.gitattributes', 'README.md', 'BUILDING.md', 'CHANGELOG.md', 'CONTRIBUTING.md', 'THIRD_PARTY_NOTICES.md', 'release/README.md', 'release/package_release.py',
          '.github/workflows/ci.yml', '.github/workflows/release.yml', '.github/workflows/issue-lifecycle.yml', '.github/ISSUE_TEMPLATE/bug_report.yml', '.github/ISSUE_TEMPLATE/config.yml', '.github/labels.json', 'tools/launcher/build_launcher.ps1'):
    t = os.path.join(HERE, 'github_templates', f.replace('/', os.sep))
    if os.path.exists(t): put(t, f)
if os.path.exists(os.path.join(ROOT, 'docs', 'CHANGELOG-2026-10-28.md')): put(os.path.join(ROOT, 'docs', 'CHANGELOG-2026-10-28.md'), 'CHANGELOG-detailed.md')
for f in ('SYNC_TO_REPO.ps1', 'README_FIRST.txt'):
    shutil.copyfile(os.path.join(HERE, 'github_templates', f), os.path.join(os.path.dirname(out), f))
for d in ('assets', 'analysis', 'mods'): text(d + '/.gitkeep', '')
# ---- deletions relative to the old repo
new = set(copied)
gone = sorted(x for x in oldlist if x and x not in new)
import re as _re
open(os.path.join(os.path.dirname(out), 'DELETED_FROM_REPO.txt'), 'w', encoding='utf-8').write('\n'.join(gone) + '\n')
print('files', len(new), 'deleted vs old repo', len(gone))
