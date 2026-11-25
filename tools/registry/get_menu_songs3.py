#!/usr/bin/env python3
"""get_menu_songs3.py -- run ON YOUR WINDOWS PC:  python get_menu_songs3.py [--music "<Custom Music folder>"]
Just Bring It (PS2): menus, Smackdown/RAW is WAR, PPV, BGM, 14 originals, opening, credits -> <Custom Music>\\Menus\\Just Bring It\\ (no wrestler entrance themes)."""
import os, sys, urllib.request, urllib.parse
MUSIC = sys.argv[sys.argv.index('--music') + 1] if '--music' in sys.argv else r'C:\Users\ajizz\Documents\Recomp\PS2Recomp_usa\build_svr06_usa\Custom Music'
SLUG = 'wwf-smackdown-just-bring-it'
L = """46.%20Smackdown%201.MP3|nxycptzo
47.%20Smackdown%202.MP3|ybipxtdi
48.%20RAW%20is%20WAR%201.MP3|gfjdqstp
49.%20RAW%20is%20WAR%202.MP3|rqdvsckr
50.%20RAW%20is%20WAR%203.MP3|pozgiyqi
51.%20Original%201.MP3|uxcxcwzh
52.%20Original%202.MP3|ovohrpyw
53.%20Original%203.MP3|ofibhffn
54.%20Original%204.MP3|vrvekkwb
55.%20Original%205.MP3|sinrvbvy
56.%20Original%206.MP3|gmrxlmei
57.%20Original%207.MP3|famjkymp
58.%20Original%208.MP3|ozhkckpw
59.%20Original%209.MP3|lthgiwkl
60.%20Original%2010.MP3|tmejjuvt
61.%20Original%2011.MP3|igkfwrtt
62.%20Original%2012.MP3|onxblbym
63.%20Original%2013.MP3|rzvekkyl
64.%20Original%2014.MP3|iitsjsni
65.%20Title%20screen.MP3|colqtuup
66.%20Main%20menu.MP3|rmjlcdlu
67.%20Wrestler%20select%20menu.MP3|ppeeiagb
68%20CAW%20menu.MP3|pugtbzsp
69.%20Season%20menu.MP3|zzzbgcvs
70.%20Pay-per-view%201.MP3|mnfjikdk
71.%20Pay-per-view%202.MP3|qxsvyfgo
72.%20Season%20showdown.MP3|bqyxsuvv
73.%20BGM%201.MP3|syoycrcn
74.%20BGM%202.MP3|fpjxoaky
75.%20BGM%203.MP3|mpmqveie
76.%20BGM%204.MP3|wvpmpobo
77.%20BGM%205.MP3|ocvxalby
78.%20BGM%206.MP3|pbqyvhbh
79.%20Opening.MP3|isqclzqf
80.%20Credits.MP3|xrrgfibg
81.%20Greatest%20wrestling%20moments.MP3|bostsfbn""".split('\n')
H = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/124 Safari/537.36', 'Referer': 'https://downloads.khinsider.com/', 'Accept': '*/*'}
d = os.path.join(MUSIC, 'Menus', 'Just Bring It'); os.makedirs(d, exist_ok=True); ok = bad = 0
for line in L:
    f, t = line.split('|'); dst = os.path.join(d, urllib.parse.unquote(f)[:-4] + '.mp3')
    if os.path.exists(dst): continue
    try:
        data = urllib.request.urlopen(urllib.request.Request('https://nu.vgmtreasurechest.com/soundtracks/%s/%s/%s' % (SLUG, t, f), headers=H), timeout=120).read()
        open(dst, 'wb').write(data); ok += 1; print('saved', os.path.basename(dst), len(data) >> 10, 'KB')
    except Exception as e: bad += 1; print('FAILED', f, e)
print('done:', ok, 'saved,', bad, 'failed')
