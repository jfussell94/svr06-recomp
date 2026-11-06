"""Extract wrestler entrance themes from the user's own SvR game copies into the port's Custom Music/Entrances folders.

usage: python extract_entrance_themes.py GAMES_DIR PROGRAM_DIR ENTRANCES_DIR [GAME ...] [--limit SECONDS]

Names come from the user's WWE Entrance Media Studio databases (PROGRAM_DIR/database/*.json + user_overrides.json);
the Studio's own readers (afs_reader.py, xwb_reader.py) are reused. One stereo copy per theme, "Wrestler - Title.mp3"
(the port shows the part before " - " and numbers a wrestler's second theme "Name 2"), album tag = the game's title so
the port can show the game year. Resumable: existing files are skipped; a time limit lets it run in chunks.
"""
import json, os, re, subprocess, sys, tempfile, time
from pathlib import Path

GAMES = {  # output folder -> (game folder, database, album title, year for the album/date tag)
    "SVR":   ("SVR",    "svr04_ps2.json", "WWE SmackDown! vs. RAW", "2004"),
    "SVR07": ("SVR 07", "svr07_x360.json", "WWE SmackDown! vs. RAW 2007", "2006"),
    "SVR08": ("SVR 08", "svr08_x360.json", "WWE SmackDown! vs. RAW 2008", "2007"),
    "SVR09": ("SVR 09", "svr09_x360.json", "WWE SmackDown! vs. RAW 2009", "2008"),
    "SVR10": ("SVR 10", "svr10_x360.json", "WWE SmackDown! vs. RAW 2010", "2009"),
}

def safe(s):
    s = re.sub(r'[<>:"/\\|?*]', "", s).strip().rstrip(".")
    return re.sub(r"\s+", " ", s)

# names as this game (SvR 2006) shows them, so versions line up across games ("Steve Austin '07", "Steve Austin '10")
ALIASES = {"stone cold": "Steve Austin", "stone cold steve austin": "Steve Austin", "vince mcmahon": "Mr. McMahon",
           "jbl": "J. B. Layfield", "john bradshaw layfield": "J. B. Layfield", "daivari": "Khosrow Daivari",
           "hhh": "Triple H", "the undertaker": "Undertaker", "rvd": "Rob Van Dam"}

def clean_name(n):
    n = safe((n or "").replace("?", "").strip())
    return ALIASES.get(n.lower(), n)

def load_db(prog, fname):
    db = json.load(open(prog / "database" / fname, encoding="utf-8"))
    try:
        ov = json.load(open(prog / "database" / "user_overrides.json", encoding="utf-8")).get(fname[:-5], {})
    except Exception:
        ov = {}
    for k, v in ov.get("superstars", {}).items():
        db.setdefault("superstars", {}).setdefault(k, {}).update(v)
    return db

def eras_title(db, name, fallback):
    e = (db.get("eras") or {}).get(name) or {}
    g = (e.get("gimmick") or "").strip()
    return g if g else fallback

def jobs_for(key, games, prog):
    gdir, dbf, album, year = GAMES[key]
    root = games / gdir
    db = load_db(prog, dbf)
    stars = db.get("superstars", {})
    jobs, seen = [], set()
    def add(name, title, kind, src, idx):
        name = clean_name(name)
        if not name or name.lower().startswith(("edit ", "motion test")):
            return
        k = (name.lower(), title.lower())
        if k in seen:
            return
        seen.add(k)
        jobs.append(dict(name=name, title=safe(title), kind=kind, src=str(src), idx=idx))
    if key == "SVR":
        for afs in ("BGM002.AFS", "BGM003.AFS"):  # entrance themes (the E1 files are arena mixes)
            ents = db["afs_music"][afs]["entries"]
            for i in sorted(ents, key=int):
                e = ents[i]
                if e.get("name"):
                    add(e["name"], eras_title(db, e["name"].replace("?", "").strip(), "Entrance Theme"), "afs", root / "BGM" / afs, int(i))
    elif key == "SVR07":
        ents = db["music"]["DVD_Music.xwb"]["entries"]
        by_slot = {}
        for e in ents:
            if e.get("ch") != 2 or "slot" not in e or e.get("ost"):
                continue
            role = e.get("role") or ""
            if role not in ("Full theme", "Theme 1", "Theme 2"):
                continue
            by_slot.setdefault(e["slot"], []).append(e)
        for slot, es in sorted(by_slot.items()):
            nm = (stars.get(str(slot)) or {}).get("name", "")
            full = [e for e in es if e.get("role") == "Full theme"]
            pick = full if full else [e for e in es if e.get("role") == "Theme 1"]
            for n, e in enumerate(pick):
                base = eras_title(db, nm.replace("?", "").strip(), "Entrance Theme")
                add(nm, base if n == 0 else f"{base} (alt)", "xwb", root / "music" / "DVD_Music.xwb", e["i"])
    else:
        for yaf, spec in db["afs_music"].items():
            ents = spec["entries"]
            for i in sorted(ents, key=int):
                e = ents[i]
                t = e.get("title") or ""
                if not e.get("name") or "5.1" in t:
                    continue
                if t.startswith("Entrance theme") or t.startswith("Tag team entrance theme"):
                    title = eras_title(db, e["name"].replace("?", "").strip(), "Entrance Theme")
                elif t.startswith("Alternate entrance theme"):
                    title = eras_title(db, e["name"].replace("?", "").strip(), "Entrance Theme") + " (alt)"
                else:
                    continue
                add(e["name"], title, "yaf", root / "sound" / yaf, int(i))
    return jobs, album, year

def raw_bytes(job, prog):
    sys.path.insert(0, str(prog))
    import afs_reader, xwb_reader
    if job["kind"] in ("afs", "yaf"):
        ents = afs_reader.read_afs(job["src"])
        e = ents[job["idx"]]
        with open(job["src"], "rb") as f:
            f.seek(e.offset); return f.read(e.size), (".adx" if job["kind"] == "afs" else ".xma")
    bank = raw_bytes.banks.get(job["src"]) or xwb_reader.parse_xwb(job["src"])
    raw_bytes.banks[job["src"]] = bank
    return xwb_reader.read_entry_wav(bank, bank.entries[job["idx"]]), ".wav"
raw_bytes.banks = {}

def main(argv):
    limit = 0
    if "--limit" in argv:
        i = argv.index("--limit"); limit = float(argv[i + 1]); del argv[i:i + 2]
    games, prog, dest = Path(argv[1]), Path(argv[2]), Path(argv[3])
    keys = argv[4:] or list(GAMES)
    t0 = time.time(); done = skipped = failed = 0
    for key in keys:
        jobs, album, year = jobs_for(key, games, prog)
        out_dir = dest / key
        out_dir.mkdir(parents=True, exist_ok=True)
        for job in jobs:
            if limit and time.time() - t0 > limit:
                print(f"PARTIAL {key}: done={done} skipped={skipped} failed={failed} (time limit)"); return 2
            out = out_dir / f"{job['name']} - {job['title']}.mp3"
            if out.exists() and out.stat().st_size > 10000:
                skipped += 1; continue
            try:
                data, ext = raw_bytes(job, prog)
                with tempfile.NamedTemporaryFile(suffix=ext, delete=False) as tf:
                    tf.write(data); tmp = tf.name
                cmd = ["ffmpeg", "-nostdin", "-y", "-loglevel", "error", "-i", tmp, "-vn", "-ac", "2", "-q:a", "2",
                       "-metadata", f"title={job['title']}", "-metadata", f"artist={job['name']}",
                       "-metadata", f"album={album}", "-metadata", f"date={year}", str(out)]
                r = subprocess.run(cmd, capture_output=True, text=True, timeout=170)
                os.unlink(tmp)
                if r.returncode == 0 and out.exists() and out.stat().st_size > 10000:
                    done += 1
                else:
                    failed += 1; print("FAIL", key, job["name"], job["title"], r.stderr[-200:])
                    if out.exists(): out.unlink()
            except Exception as ex:
                failed += 1; print("FAIL", key, job["name"], job["title"], ex)
        print(f"{key}: {len(jobs)} themes (done {done}, skipped {skipped}, failed {failed} so far)")
    print(f"ALL DONE done={done} skipped={skipped} failed={failed}")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
