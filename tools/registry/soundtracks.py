#!/usr/bin/env python3
"""soundtracks.py -- song-ID namespace (parallel to the wrestler CIDs in wrestlers.json), written to soundtracks.json.

SID = block*1000 + n, block = release block from romcat.RELEASES (same release-year order, SmackDown! ... WWE 2K17).
  n 0..199    stock soundtrack of that game.  Where the game addresses music by (bank, entry) the offset is bank*40+entry, i.e. for
              SvR 2006 the SID is 9000 + the entrance-music byte of the PRO record (CL-0317).
  n 200..899  OPEN: songs imported for that game's slot range (menu / entrance music a player brings from that game)
  n 900..999  reserved for the game's alternate mixes (ENVB/ENVH/E1 variants map to base SID + 900 offset by the game itself)
  block 90    OPEN custom soundtracks: SID 90000..99999 (user songs from the Custom Music folder)
Menu music uses SIDs: a song whose file has not been imported is skipped (the player simply goes to the next SID)."""
import os, sys, re, json, struct
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import isox, romcat
D = os.path.expanduser("~/mnt/Desktop/Games Mods and Recomp Library/Games and ISOs/WWE and SmackDown ISOs/PS2")
def afs_count(iso, lba):
    with open(iso, 'rb') as f: f.seek(lba * 2048); h = f.read(8)
    return struct.unpack('<I', h[4:8])[0] if h[:3] == b'AFS' else 0
def scan(fn, block):
    p = os.path.join(D, fn); e = isox.entries(p); songs = []; banks = {}
    for k, (lba, sz) in sorted(e.items()):
        u = k.upper(); m = re.search(r'/BGM(\d)?(\d{3})?(E1|ENVB|ENVH)?\.AFS$', u) or re.search(r'/BGM(\d)(ENV[BH])?\.AFS$', u)
        mb = re.search(r'/BGM(\d{1,3})(E1|ENVB|ENVH)?\.AFS$', u)
        if mb:
            bank = int(mb.group(1)); var = mb.group(2) or ''; n = afs_count(p, lba)
            if bank == 999: base = block * 1000 + 190
            else: base = block * 1000 + (bank if bank < 10 else bank) * 40 if bank < 5 else block * 1000 + 190
            banks[os.path.basename(k)] = {'bank': bank, 'variant': var, 'entries': n, 'sidBase': base}
            if not var:
                songs += [{'sid': base + i, 'bank': bank, 'entry': i} for i in range(n)]
        m3 = re.search(r'/ADS/(\d)/(.+)\.ADS$', u)
        if m3:
            num = re.search(r'(\d+)', m3.group(2)); n = int(num.group(1)) if num else 0
            songs.append({'sid': block * 1000 + n, 'file': k.lstrip('/'), 'ads': m3.group(2)})
    return {'banks': banks, 'songs': songs}
def main(out):
    games = {'hctp': ('WWE SmackDown! Here Comes the Pain (Europe).iso', 5), 'svr05': ('WWE SmackDown! vs. Raw (Europe, Australia).iso', 7),
             'svr06u': ('WWE SmackDown! vs. Raw 2006 (USA).iso', 9), 'jbi': ('WWF SmackDown! Just Bring It (Europe, Australia).iso', 3)}
    res = {'version': 1, 'scheme': __doc__, 'customBlock': 90, 'customRange': [90000, 99999], 'blocks': []}
    scanned = {}
    for k, (fn, b) in games.items():
        try: scanned[b] = scan(fn, b)
        except Exception as ex: scanned[b] = {'error': str(ex)}
    for b, t, y, pl, st in romcat.RELEASES:
        s = scanned.get(b, {})
        res['blocks'].append({'block': b, 'title': t, 'year': y, 'status': 'scanned' if s.get('songs') else 'reserved',
                              'sidBase': b * 1000, 'stockRange': [b * 1000, b * 1000 + 199], 'openRange': [b * 1000 + 200, b * 1000 + 899],
                              'altMixRange': [b * 1000 + 900, b * 1000 + 999], 'banks': s.get('banks', {}), 'songs': s.get('songs', [])})
    json.dump(res, open(out, 'w', encoding='utf-8'), indent=1)
    print({b['block']: len(b['songs']) for b in res['blocks'] if b['songs']})
if __name__ == '__main__': main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, 'soundtracks.json'))
