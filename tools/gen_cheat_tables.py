#!/usr/bin/env python3
"""Regenerate PS2Recomp/ps2xRuntime/src/lib/ps2_cheat_tables.h from tools/launcher/server.py (SHOP_ITEMS + CHEATS).
Usage: python gen_cheat_tables.py [path-to-ps2_cheat_tables.h ...]   (default: both PAL and USA runtime trees if present)
The launcher stays the single source of truth; run this after editing SHOP_ITEMS / CHEATS there."""
import ast, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.path.join(HERE, 'launcher', 'server.py')
src = open(SERVER, encoding='utf-8').read()

def grab(name, env=None):
    m = re.search(r'^%s = ' % name, src, re.M)
    i = m.end(); depth = 0; j = i
    while True:
        c = src[j]
        if c in '[{(': depth += 1
        elif c in ')]}': depth -= 1
        elif c in '"\'':   # skip string literal
            q = c; j += 1
            while src[j] != q:
                j += 2 if src[j] == '\\' else 1
        j += 1
        if depth == 0 and j > i: break
    return eval(src[i:j], {'__builtins__': {}}, env or {})

SHOP = grab('SHOP_ITEMS'); CHEATS = grab('CHEATS', {'SHOP_ITEMS': SHOP})
q = lambda s: '"' + str(s).replace('\\', '\\\\').replace('"', '\\"') + '"'
shop_lines, first = [], {}
for cid, items in SHOP.items():
    first[cid] = (len(shop_lines), len(items))
    shop_lines += ['    {%d, %s},' % (n, q(name)) for n, name in items]
out = ['// GENERATED from tools/launcher/server.py (CHEATS + SHOP_ITEMS). Shared by the F1 menu and the native cheat applier.',
       '#pragma once', '#include <cstdint>', 'namespace svr06cheat {', 'struct ShopItem { int n; const char *name; };',
       'struct CheatDef { const char *grp, *id, *name, *desc; bool per; int kind; const char *status; int first, count; }; // kind 0 switch, 1 choice (off/1/2/unlimited), 2 checks',
       'inline const ShopItem kShop[] = {'] + shop_lines + ['};', 'inline const CheatDef kCheats[] = {']
for c in CHEATS:
    kind = {'switch': 0, 'choice': 1, 'checks': 2}[c['kind']]
    f, n = first.get(c['id'], (0, 0))
    out.append('    {%s, %s, %s, %s, %s, %d, %s, %d, %d},' % (q(c['g']), q(c['id']), q(c['name']), q(c.get('desc', '')), 'true' if c.get('per') else 'false', kind, q(c.get('status', 'todo')), f, n))
out += ['};', 'inline constexpr int kCheatCount = %d;' % len(CHEATS), '}', '']
text = '\n'.join(out)
targets = sys.argv[1:] or [p for p in (os.path.join(HERE, '..', '..', 'PS2Recomp', 'ps2xRuntime', 'src', 'lib', 'ps2_cheat_tables.h'),
                                       os.path.join(HERE, '..', '..', 'PS2Recomp_usa', 'ps2xRuntime', 'src', 'lib', 'ps2_cheat_tables.h')) if os.path.isdir(os.path.dirname(p))]
for t in targets:
    open(t, 'w', encoding='utf-8', newline='\n').write(text); print('wrote', os.path.normpath(t), len(CHEATS), 'cheats', len(shop_lines), 'shop items')
