#!/usr/bin/env python3
"""get_menu_songs.py -- run ON YOUR WINDOWS PC:  python get_menu_songs.py [--music "<Custom Music folder>"]
Downloads the confirmed licensed-soundtrack songs of SvR 2006 and SvR 2007 (KHInsider PS2 gamerips, not the 'Album Version' albums)
as mp3 into  <Custom Music>\\Menus\\<game>\\Artist - Title.mp3.  Links were read from the album pages; artist/title come from the Entrance Studio
databases (confirmed entries only).  SvR 2008: the PS2 album on KHInsider is unnamed (01.mp3..75.mp3), so nothing is downloaded for it."""
import os, sys, urllib.request, urllib.parse
MUSIC = sys.argv[sys.argv.index('--music') + 1] if '--music' in sys.argv else r'C:\Users\ajizz\Documents\Recomp\PS2Recomp_usa\build_svr06_usa\Custom Music'
U6 = 'https://jetta.vgmtreasurechest.com/soundtracks/wwe-smackdown-vs.-raw-2006-ps2-gamerip/'
U7 = 'https://lambda.vgmtreasurechest.com/soundtracks/wwe-smackdown-vs-raw-2007-ps2-gamerip/'
Q = urllib.parse.quote
T = {
 'SVR 2006': [
  ('Bumpy Knuckles - Bang Bang', U6 + 'mtfxugwp/' + Q('01-Bang Bang.mp3')),
  ('Ron J - I Ain\'t Going Nowhere', U6 + 'ljmhbjqb/' + Q("02-I Ain't Goin Nowhere.mp3")),
  ('Billy Ray - Scream at Me', U6 + 'mfpjzxoc/' + Q('04-Scream At Me.mp3')),
  ('Poet - Crush Kill Destroy', U6 + 'silheyez/' + Q('06-Crush Kill Destroy.mp3')),
  ('Vada - King of the Mat', U6 + 'kcheygia/' + Q('08-King Of The Mat.mp3')),
  ('Bumpy Knuckles - You Don\'t Want War', U6 + 'aacmznqr/' + Q('10-You Dont Want War.mp3')),
  ('Fireball Ministry - The Broken', U6 + 'jxkjpyug/' + Q('12-The Broken.mp3')),
  ('The Dillinger Escape Plan - Unretrofied', U6 + 'wmvtfuhc/' + Q('13-Unretrofied.mp3')),
  ('Dark New Day - Pieces', U6 + 'raaclzli/' + Q('14-Pieces.mp3')),
  ('Not Forgotten - Waiting', U6 + 'kxkgahml/' + Q('15-Waiting.mp3')),
  ('Megadeth - Symphony of Destruction (Steve Tushar Remix)', U6 + 'walfqpdt/' + Q('16-Symphony Of Destruction (Remix).mp3')),
  ('Static-X - Start a War', U6 + 'mdeodcbz/' + Q('17-Start A War.mp3')),
 ],
 'SVR 2007': [
  ('Nonpoint - Alive & Kicking', U7 + 'ihntfuhb/' + Q('1-01. Alive and Kicking.mp3')),
  ('Three Days Grace - Animal I Have Become', U7 + 'hqiuxdyi/' + Q("1-02. Animal I've Become.mp3")),
  ('Nonpoint - Bullet with a Name', U7 + 'fhvelrae/' + Q('1-03. Bullet With a Name.mp3')),
  ('Ghostface Killah - The Champ', U7 + 'ddenbtnk/' + Q('1-04. The Champ.mp3')),
  ('Godsmack - The Enemy', U7 + 'bbfuigjc/' + Q('1-05. The Enemy.mp3')),
  ('Versus the World - Forgive Me', U7 + 'sorrleoe/' + Q('1-06. Forgive Me.mp3')),
  ('Bullets and Octane - I Ain\'t Your Savior', U7 + 'yzzbhhns/' + Q("1-07. I Ain't Your Savior.mp3")),
  ('Black Stone Cherry - Lonely Train', U7 + 'kvupmtgy/' + Q('1-08. Lonely Train.mp3')),
  ('Lil Scrappy - Money in the Bank', U7 + 'fmxzhlhf/' + Q('1-09. Money in the Bank.mp3')),
  ('Three Days Grace - Riot', U7 + 'wiafxwsz/' + Q('1-10. Riot.mp3')),
  ('Allele - Stitches', U7 + 'sbgzcrac/' + Q('1-11. Stitches.mp3')),
  ('Rise Against - Survive', U7 + 'mgswdejg/' + Q('1-12. Survive.mp3')),
 ],
}
H = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/124 Safari/537.36', 'Referer': 'https://downloads.khinsider.com/', 'Accept': '*/*'}
ok = bad = 0
for game, items in T.items():
    d = os.path.join(MUSIC, 'Menus', game); os.makedirs(d, exist_ok=True)
    for name, url in items:
        dst = os.path.join(d, name.replace('"', '') + '.mp3')
        if os.path.exists(dst): print('have', name); continue
        try:
            data = urllib.request.urlopen(urllib.request.Request(url, headers=H), timeout=120).read()
            open(dst, 'wb').write(data); ok += 1; print('saved', game, '-', name, len(data) >> 10, 'KB')
        except Exception as e: bad += 1; print('FAILED', name, e)
print('done:', ok, 'saved,', bad, 'failed')
