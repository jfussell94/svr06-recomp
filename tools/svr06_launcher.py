#!/usr/bin/env python3
"""SvR06 Launcher v2 (Python/Tkinter, stays Python until the 1.0-preview). Start with:  pythonw tools\\svr06_launcher.py
A thin front end over what already exists: scripts\\run.cmd (play), scripts\\FAST_REBUILD_AND_RUN.cmd (rebuild),
tools\\svrlive.py (live console: quit / shot / perf) and tools\\scenario.py (scripted freeze test).
The status line reads the presenter's heartbeat (analysis\\live\\heartbeat.txt, ISS-146 build or newer)."""
import os, re, sys, time, json, subprocess, zipfile, threading, tkinter as tk
from tkinter import ttk, filedialog, messagebox

HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import svrlive
LIVE = svrlive.LIVE
MODES = {   # name -> SVR06_FLAGS (debug bits, see CHANGELOG): the safe default adds nothing
    'Normal (VU1 speed-ups on)': 0,
    'Safe: VU1 speed-ups off (CL-0133 off)': 1 << 29,   # 2026-11-07 ISS-151: speed-ups are on by default; flag 29 now switches them off
}
ICON_SETS = ['Original (PS2)', 'Xbox One', 'Xbox 360', 'PS3', 'PS4', 'PS5', 'Switch Pro', 'Switch Pro 2', 'Custom (mods/icons/mypack)']
ICON_ENV = {'Original (PS2)': '', 'Xbox One': 'xboxone', 'Xbox 360': 'xbox360', 'PS3': 'ps3', 'PS4': 'ps4', 'PS5': 'ps5', 'Switch Pro': 'switchpro', 'Switch Pro 2': 'switchpro2', 'Custom (mods/icons/mypack)': 'mypack'}
TEXPACK_DIR = os.path.join(ROOT, 'mods', 'texturepack', 'emulation_lovers_hd', 'replacements')
PREFS = os.path.join(ROOT, 'config', 'launcher_prefs.json')
NOWIN = 0x08000000 if os.name == 'nt' else 0   # CREATE_NO_WINDOW

PROBE_NOTE = 'Boot-freeze workaround (ISS-160): keep ON until the real fix lands'
def count_files(d, ext=None):
    try: return sum(1 for f in os.listdir(d) if (ext is None or f.lower().endswith(ext)))
    except OSError: return 0
def ago(t):
    d = max(0, time.time() - t)
    return '%dm ago' % (d // 60) if d < 3600 else ('%.1fh ago' % (d / 3600) if d < 86400 else '%dd ago' % (d // 86400))

class App(tk.Tk):
    def __init__(self):
        super().__init__(); self.title('SvR06 Recomp Launcher'); self.geometry('720x640'); self.minsize(680, 600)
        try: pr = json.load(open(PREFS))
        except Exception: pr = {}
        self.pr = pr
        top = ttk.Frame(self, padding=(12, 10, 12, 0)); top.pack(fill='x')
        ttk.Label(top, text='WWE SmackDown! vs. Raw 2006 - native recomp', font=('Segoe UI', 13, 'bold')).pack(anchor='w')
        self.status = tk.Label(top, text='status: ?', anchor='w', font=('Consolas', 10), padx=8, pady=6, bg='#444', fg='white'); self.status.pack(fill='x', pady=(6, 4))
        bt = ttk.Frame(top); bt.pack(fill='x')
        self.btn = {}
        for i, (t, c) in enumerate([('Play', self.play), ('Rebuild + play', self.rebuild), ('Rebuild only', self.build_only), ('Quit game', self.quit_game)]):
            x = ttk.Button(bt, text=t, command=c); x.grid(row=0, column=i, sticky='we', padx=2, pady=3, ipady=4); bt.columnconfigure(i, weight=1)
        pf = ttk.Frame(top); pf.pack(fill='x', pady=(4, 4))
        st = ttk.Style(self)
        try: st.theme_use('clam')
        except tk.TclError: pass
        st.configure('Green.Horizontal.TProgressbar', troughcolor='#2b2b2b', background='#2fb344', bordercolor='#2b2b2b', lightcolor='#2fb344', darkcolor='#2fb344')
        self.bar = ttk.Progressbar(pf, style='Green.Horizontal.TProgressbar', orient='horizontal', length=260, mode='determinate', maximum=100); self.bar.pack(side='left')
        self.bartxt = tk.Label(pf, text='no build running', anchor='w', font=('Consolas', 9)); self.bartxt.pack(side='left', padx=8)
        self.build_t0 = None; self.build_seen = None
        nb = ttk.Notebook(self); nb.pack(fill='both', expand=True, padx=12, pady=8)
        self.nb = nb
        self.t_play = ttk.Frame(nb, padding=10); self.t_tools = ttk.Frame(nb, padding=10); self.t_log = ttk.Frame(nb, padding=6); self.t_info = ttk.Frame(nb, padding=10)
        for t, n in ((self.t_play, 'Play options'), (self.t_tools, 'Tools'), (self.t_log, 'Logs'), (self.t_info, 'Health check')): nb.add(t, text=n)
        self.build_play_tab(); self.build_tools_tab(); self.build_log_tab(); self.build_info_tab()
        self.out = self.launch_out
        self.after(500, self.poll); self.after(1500, self.refresh_info)

    # ---------------- tabs
    def build_play_tab(self):
        f = self.t_play; pr = self.pr
        ttk.Label(f, text='Performance mode').grid(row=0, column=0, sticky='w')
        self.mode = tk.StringVar(value=pr.get('mode') if pr.get('mode') in MODES else list(MODES)[0])
        ttk.Combobox(f, textvariable=self.mode, values=list(MODES), state='readonly', width=44).grid(row=0, column=1, sticky='we', padx=6, pady=3)
        ttk.Separator(f).grid(row=1, column=0, columnspan=3, sticky='we', pady=8)
        ttk.Label(f, text='Controller button icons', font=('Segoe UI', 10, 'bold')).grid(row=2, column=0, columnspan=2, sticky='w')
        self.icons = tk.StringVar(value=pr.get('icons') if pr.get('icons') in ICON_SETS else ICON_SETS[0])
        cb = ttk.Combobox(f, textvariable=self.icons, values=ICON_SETS, state='readonly', width=34); cb.grid(row=3, column=0, columnspan=2, sticky='w', pady=3)
        cb.bind('<<ComboboxSelected>>', lambda e: self.show_icon_preview())
        self.icon_note = ttk.Label(f, text='', foreground='#888'); self.icon_note.grid(row=4, column=0, columnspan=2, sticky='w')
        self.prev = tk.Label(f, bg='#1e1e1e', width=24, height=12, text='preview'); self.prev.grid(row=2, column=2, rowspan=5, padx=10, sticky='ne')
        self.prev_img = None
        ttk.Separator(f).grid(row=7, column=0, columnspan=3, sticky='we', pady=8)
        ttk.Label(f, text='Textures', font=('Segoe UI', 10, 'bold')).grid(row=8, column=0, columnspan=2, sticky='w')
        self.texpack = tk.BooleanVar(value=bool(pr.get('texpack', False)))
        ttk.Checkbutton(f, text='HD texture pack: detect matches (stage 1: logs only, no visual change yet)', variable=self.texpack).grid(row=9, column=0, columnspan=3, sticky='w')
        self.tex_note = ttk.Label(f, text='', foreground='#888'); self.tex_note.grid(row=10, column=0, columnspan=3, sticky='w')
        ttk.Separator(f).grid(row=11, column=0, columnspan=3, sticky='we', pady=8)
        ttk.Label(f, text='Startup', font=('Segoe UI', 10, 'bold')).grid(row=12, column=0, columnspan=2, sticky='w')
        self.probes = tk.BooleanVar(value=bool(pr.get('probes', True)))
        ttk.Checkbutton(f, text=PROBE_NOTE, variable=self.probes).grid(row=13, column=0, columnspan=3, sticky='w')
        self.skipmov = tk.BooleanVar(value=bool(pr.get('skipmovies', False)))
        ttk.Checkbutton(f, text='Skip intro movies', variable=self.skipmov).grid(row=14, column=0, columnspan=3, sticky='w')
        self.save_btn = ttk.Button(f, text='Save these choices as default', command=self.save_prefs); self.save_btn.grid(row=15, column=0, sticky='w', pady=(12, 0))
        self.envlbl = ttk.Label(f, text='', foreground='#888', wraplength=520, justify='left'); self.envlbl.grid(row=16, column=0, columnspan=3, sticky='w', pady=(6, 0))
        f.columnconfigure(1, weight=1)
        for v in (self.mode, self.icons, self.texpack, self.probes, self.skipmov): v.trace_add('write', lambda *a: self.update_env_label())
        self.show_icon_preview(); self.update_env_label()

    def build_tools_tab(self):
        f = self.t_tools
        groups = [('In-game', [('Screenshot', lambda: self.live('shot')), ('Perf snapshot', lambda: self.live('perf')), ('Run test scenario...', self.scenario)]),
                  ('Folders', [('Logs (analysis)', lambda: self.open_path(os.path.join(ROOT, 'analysis'))), ('Screenshots', lambda: self.open_path(os.path.join(ROOT, 'analysis', 'shots'))),
                               ('Icon sets (mods/icons)', lambda: self.open_path(os.path.join(ROOT, 'mods', 'icons'))), ('Texture pack folder', lambda: self.open_path(os.path.join(ROOT, 'mods', 'texturepack'))),
                               ('Project root', lambda: self.open_path(ROOT))]),
                  ('Support', [('Collect bug report (zip)', self.report), ('Open changelog', self.changelog), ('Open icon preview sheet', lambda: self.open_path(os.path.join(ROOT, 'mods', 'icons', 'preview_all.png'))),
                               ('Clear old logs (keep last 20)', self.prune_logs)])]
        r = 0
        for title, items in groups:
            ttk.Label(f, text=title, font=('Segoe UI', 10, 'bold')).grid(row=r, column=0, columnspan=3, sticky='w', pady=(8 if r else 0, 2)); r += 1
            for i, (t, c) in enumerate(items):
                ttk.Button(f, text=t, command=c).grid(row=r + i // 3, column=i % 3, sticky='we', padx=2, pady=2)
            r += (len(items) + 2) // 3
        for i in range(3): f.columnconfigure(i, weight=1)
        ttk.Label(f, text='Launcher messages').grid(row=r, column=0, sticky='w', pady=(10, 2)); r += 1
        self.launch_out = tk.Text(f, height=7, font=('Consolas', 9)); self.launch_out.grid(row=r, column=0, columnspan=3, sticky='nsew'); f.rowconfigure(r, weight=1)

    def build_log_tab(self):
        f = self.t_log
        bar = ttk.Frame(f); bar.pack(fill='x')
        ttk.Label(bar, text='Show lines containing:').pack(side='left')
        self.logfilter = tk.StringVar(value=self.pr.get('logfilter', '[texpack]|[iconhook]'))
        e = ttk.Entry(bar, textvariable=self.logfilter, width=34); e.pack(side='left', padx=6)
        self.logsrc = tk.StringVar(value='run_err.txt')
        ttk.Combobox(bar, textvariable=self.logsrc, values=['run_err.txt', 'run_out.txt', 'build_log.txt'], state='readonly', width=14).pack(side='left')
        ttk.Button(bar, text='Refresh', command=self.refresh_log).pack(side='left', padx=6)
        self.autolog = tk.BooleanVar(value=False)
        ttk.Checkbutton(bar, text='auto', variable=self.autolog).pack(side='left')
        self.logtxt = tk.Text(f, font=('Consolas', 9), wrap='none'); sb = ttk.Scrollbar(f, command=self.logtxt.yview); self.logtxt.configure(yscrollcommand=sb.set)
        sb.pack(side='right', fill='y'); self.logtxt.pack(fill='both', expand=True, pady=(6, 0))
        self.logcount = ttk.Label(f, text=''); self.logcount.pack(anchor='w')

    def build_info_tab(self):
        f = self.t_info
        self.info = tk.Text(f, font=('Consolas', 10), height=22, state='disabled'); self.info.pack(fill='both', expand=True)
        self.info.tag_configure('ok', foreground='#2fb344'); self.info.tag_configure('bad', foreground='#d94141'); self.info.tag_configure('warn', foreground='#d9a441')
        ttk.Button(f, text='Re-check', command=self.refresh_info).pack(anchor='w', pady=(6, 0))

    # ---------------- helpers
    def open_path(self, p):
        try:
            if not os.path.exists(p) and not p.endswith('.png'): os.makedirs(p, exist_ok=True)
            os.startfile(p)
        except Exception as e: self.log('open failed: %s' % e)
    def build_only(self):
        self.build_t0 = time.time(); self.build_seen = None
        self.spawn('"%s"' % os.path.join(ROOT, 'scripts', 'BUILD_ONLY.cmd'), 'rebuild only')
    def prune_logs(self):
        d = os.path.join(ROOT, 'analysis'); fs = sorted([f for f in os.listdir(d) if f.startswith('prev_') and f.endswith(('_run_err.txt', '_run_out.txt'))])
        old = fs[:-20]; n = 0
        for f in old:
            try: os.remove(os.path.join(d, f)); n += 1
            except OSError: pass
        self.log('old logs: removed %d of %d (windows may deny deletes; the rest stay)' % (n, len(fs)))
    def save_prefs(self):
        try:
            os.makedirs(os.path.dirname(PREFS), exist_ok=True)
            json.dump({'mode': self.mode.get(), 'icons': self.icons.get(), 'texpack': self.texpack.get(), 'probes': self.probes.get(), 'skipmovies': self.skipmov.get(), 'logfilter': self.logfilter.get()}, open(PREFS, 'w'))
            self.log('saved defaults')
        except Exception as e: self.log('save failed: %s' % e)
    def show_icon_preview(self):
        ic = ICON_ENV.get(self.icons.get(), '')
        png = os.path.join(ROOT, 'mods', 'icons', ic, 'glyph_sheet.png') if ic else os.path.join(ROOT, '..', 'svr06-recomp', 'analysis', 'glyph', 'tex', 'tbp7936_tbw2_psm19_w128_h256_cbp8064.png')
        ok = ic == '' or os.path.exists(os.path.join(ROOT, 'mods', 'icons', ic, 'glyph_idx.bin'))
        self.icon_note.config(text=('original game icons' if not ic else ('ready: mods/icons/%s' % ic if ok else 'MISSING files in mods/icons/%s - run tools/make_icon_sheets.py' % ic)), foreground='#888' if ok else '#d94141')
        try:
            self.prev_img = tk.PhotoImage(file=png); self.prev.config(image=self.prev_img, text='', width=self.prev_img.width(), height=self.prev_img.height())
        except Exception: self.prev_img = None; self.prev.config(image='', text='no preview', width=24, height=12)
    def update_env_label(self):
        try:
            e = self.env(save=False)
            keys = ['SVR06_FLAGS', 'SVR06_ICONSET', 'SVR06_TEXPACK', 'SVR06_NOPROBES', 'SVR06_SKIPMOVIES']
            self.envlbl.config(text='Will launch with: ' + ', '.join('%s=%s' % (k.replace('SVR06_', ''), os.path.basename(e[k]) if k == 'SVR06_TEXPACK' else e[k]) for k in keys if k in e) if any(k in e for k in keys) else 'Will launch with: defaults')
            n = count_files(TEXPACK_DIR, '.png')
            self.tex_note.config(text=('pack found: %d textures' % n) if n else 'pack folder is empty or missing: mods/texturepack/emulation_lovers_hd/replacements', foreground='#888' if n else '#d94141')
        except Exception: pass

    def log(self, s): self.launch_out.insert('end', s.rstrip() + '\n'); self.launch_out.see('end')
    def env(self, save=True):
        e = dict(os.environ); fl = MODES[self.mode.get()]
        if fl: e['SVR06_FLAGS'] = str(fl)
        else: e.pop('SVR06_FLAGS', None)
        ic = ICON_ENV.get(self.icons.get(), '')
        if ic: e['SVR06_ICONSET'] = ic; e['SVR06_ICONDIR'] = os.path.join(ROOT, 'mods', 'icons')
        else: e.pop('SVR06_ICONSET', None)
        if self.texpack.get(): e['SVR06_TEXPACK'] = TEXPACK_DIR
        else: e.pop('SVR06_TEXPACK', None)
        if self.probes.get(): e.pop('SVR06_NOPROBES', None)
        else: e['SVR06_NOPROBES'] = '1'; e.pop('SVR06_PROBES', None)
        if self.skipmov.get(): e['SVR06_SKIPMOVIES'] = '1'
        else: e.pop('SVR06_SKIPMOVIES', None)
        if save: self.save_prefs()
        return e
    def spawn(self, cmd, label):
        self.log('%s: %s' % (label, cmd)); subprocess.Popen(cmd, shell=True, cwd=ROOT, env=self.env())
    def play(self): self.spawn('"%s"' % os.path.join(ROOT, 'scripts', 'run.cmd'), 'play (' + self.mode.get() + ')')
    def rebuild(self):
        self.build_t0 = time.time(); self.build_seen = None
        self.spawn('"%s"' % os.path.join(ROOT, 'scripts', 'FAST_REBUILD_AND_RUN.cmd'), 'rebuild + play')
    def live(self, c):
        def job():
            r = svrlive.send([c if c != 'shot' else 'shot launcher_' + time.strftime('%H%M%S')], 12)
            self.after(0, lambda: self.log('%s -> %s' % (c, (r or 'no answer (game not running or frozen)').strip())))
        threading.Thread(target=job, daemon=True).start()
    def quit_game(self):
        threading.Thread(target=lambda: self.after(0, lambda: self.log('quit -> %s' % (svrlive.send(['quit'], 6) or 'no answer, taskkill'))), daemon=True).start()
        subprocess.Popen('taskkill /IM svr06run.exe', shell=True, creationflags=NOWIN)
    def scenario(self):
        p = filedialog.askopenfilename(initialdir=os.path.join(HERE, 'scenarios'), filetypes=[('scenario', '*.txt')])
        if p: self.spawn('python "%s" "%s"' % (os.path.join(HERE, 'scenario.py'), p), 'scenario')
    def open_logs(self):
        d = os.path.join(ROOT, 'analysis'); os.makedirs(d, exist_ok=True)
        try: os.startfile(d)
        except Exception as e: self.log('open failed: %s' % e)
    def changelog(self):
        try: os.startfile(os.path.join(ROOT, 'docs', 'CHANGELOG-2026-10-28.md'))
        except Exception as e: self.log('open failed: %s' % e)
    def report(self):
        out = os.path.join(ROOT, 'analysis', 'bug_report_%s.zip' % time.strftime('%Y%m%d_%H%M%S')); n = 0
        with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:   # logs, live console output, heartbeat, recent screenshots - never game files or saves
            z.writestr('INFO.txt', 'mode=%s\ntime=%s\n' % (self.mode.get(), time.ctime()))
            for d in (LIVE, os.path.join(LIVE, '..'), os.path.join(ROOT, 'analysis', 'scenario')):
                if not os.path.isdir(d): continue
                for f in sorted(os.listdir(d)):
                    p = os.path.join(d, f)
                    if os.path.isfile(p) and f.lower().endswith(('.log', '.txt', '.zip')) and os.path.getsize(p) < 50_000_000:
                        z.write(p, os.path.basename(os.path.dirname(os.path.abspath(p))) + '/' + f); n += 1
            sh = os.path.join(LIVE, 'shots')
            if os.path.isdir(sh):
                for f in sorted(os.listdir(sh))[-5:]: z.write(os.path.join(sh, f), 'shots/' + f); n += 1
        self.log('bug report: %s (%d files)' % (out, n))
    @staticmethod
    def fmt(sec): sec = int(max(0, sec)); return '%d:%02d' % (sec // 60, sec % 60)
    def build_progress(self):
        lg = os.path.join(ROOT, 'analysis', 'build_log.txt')
        try: mt = os.path.getmtime(lg); sz = os.path.getsize(lg)
        except OSError: return
        with open(lg, 'rb') as fh:
            fh.seek(max(0, sz - 65536)); tail = fh.read().decode('utf-8', 'replace')
        done = 'BUILD STEP FINISHED' in tail
        m = re.findall(r'\[(\d+)/(\d+)\]', tail); n, tot = (int(m[-1][0]), int(m[-1][1])) if m else (0, 0)
        live = (time.time() - mt) < 20 and not done                    # log still being written = a build is running (also when started from the shortcut)
        if live and self.build_t0 is None: self.build_t0 = mt if self.build_seen is None else time.time()
        if live and tot:
            el = time.time() - self.build_t0; est = el * tot / n if n else 0
            self.bar['value'] = 100.0 * n / tot
            self.bartxt.config(text='%d/%d files   %s / ~%s' % (n, tot, self.fmt(el), self.fmt(est) if n else '?'), fg='#2fb344')
        elif live: self.bartxt.config(text='configuring / starting...', fg='#2fb344')
        elif self.build_t0 is not None and done and mt >= self.build_t0 - 1:   # ignore the previous build's finished log until the new one starts writing
            el = mt - self.build_t0; bad = bool(re.search(r'(?<![A-Za-z-])error:', tail))
            self.bar['value'] = 100.0 if not bad else self.bar['value']; self.bartxt.config(text=('FAILED, see build_log.txt' if bad else 'build done') + '  %d/%d files  %s' % (n, tot, self.fmt(el)), fg='#9a1c1c' if bad else '#2fb344'); self.build_t0 = None
        self.build_seen = True

    def refresh_log(self):
        p = os.path.join(ROOT, 'analysis', self.logsrc.get()); pats = [x for x in self.logfilter.get().split('|') if x]
        try:
            sz = os.path.getsize(p)
            with open(p, 'rb') as fh:
                fh.seek(max(0, sz - 4_000_000)); lines = fh.read().decode('utf-8', 'replace').splitlines()
        except OSError as e:
            self.logtxt.delete('1.0', 'end'); self.logtxt.insert('end', 'cannot read %s: %s' % (p, e)); return
        sel = [l for l in lines if (not pats or any(x in l for x in pats))]
        self.logtxt.delete('1.0', 'end'); self.logtxt.insert('end', '\n'.join(sel[-800:])); self.logtxt.see('end')
        self.logcount.config(text='%d matching lines (showing last %d) of the last 4 MB of %s' % (len(sel), min(len(sel), 800), self.logsrc.get()))
    def refresh_info(self):
        t = self.info; t.config(state='normal'); t.delete('1.0', 'end')
        def line(ok, msg):
            t.insert('end', ('[ OK ] ' if ok is True else ('[WARN] ' if ok is None else '[FAIL] ')) + msg + '\n', 'ok' if ok is True else ('warn' if ok is None else 'bad'))
        rt = os.path.abspath(os.path.join(ROOT, '..', 'PS2Recomp', 'build_svr06b_clang', 'ps2xRuntime', 'ps2EntryRunner.exe'))
        if os.path.exists(rt): line(True, 'game build: %s (%s, %.0f MB)' % (time.strftime('%Y-%m-%d %H:%M', time.localtime(os.path.getmtime(rt))), ago(os.path.getmtime(rt)), os.path.getsize(rt) / 1e6))
        else: line(False, 'game executable not built yet (use Rebuild + play)')
        elf = None
        ad = os.path.abspath(os.path.join(ROOT, '..', 'assets'))
        for dp, dn, fn in os.walk(ad):
            if 'SLES_536.76' in fn: elf = os.path.join(dp, 'SLES_536.76'); break
            if dp.count(os.sep) - ad.count(os.sep) > 3: dn[:] = []
        line(bool(elf), 'PAL boot file SLES_536.76: ' + (elf or 'NOT found under assets\\'))
        for k, v in ICON_ENV.items():
            if v: line(os.path.exists(os.path.join(ROOT, 'mods', 'icons', v, 'glyph_idx.bin')) and os.path.exists(os.path.join(ROOT, 'mods', 'icons', v, 'glyph_clut.bin')), 'icon set %-12s' % k)
        line(os.path.exists(os.path.join(ROOT, 'mods', 'icons', 'orig_idx.bin')), 'icon reference data (orig_idx/orig_clut)')
        n = count_files(TEXPACK_DIR, '.png'); line(n > 0 or None, 'HD texture pack: %d PNG files' % n)
        bl = os.path.join(ROOT, 'analysis', 'build_log.txt')
        if os.path.exists(bl):
            tail = open(bl, 'rb').read()[-65536:].decode('utf-8', 'replace'); done = 'BUILD STEP FINISHED' in tail; bad = bool(re.search(r'(?<![A-Za-z-])error:', tail))
            line(None if not done else (not bad), 'last build log (%s): %s' % (ago(os.path.getmtime(bl)), 'finished' if done and not bad else ('FAILED' if bad else 'did not reach the finished marker (interrupted or still running)')))
        re_ = os.path.join(ROOT, 'analysis', 'run_err.txt')
        if os.path.exists(re_):
            tx = open(re_, 'rb').read()[-4_000_000:]
            line(True, 'last run log: %s, %d [texpack] lines, %d [iconhook] lines' % (ago(os.path.getmtime(re_)), tx.count(b'[texpack]'), tx.count(b'[iconhook]')))
        try:
            import importlib; importlib.import_module('PIL'); line(True, 'Pillow available (icon tools)')
        except Exception: line(None, 'Pillow not installed (only needed for tools/make_icon_*.py)')
        t.config(state='disabled')
    def poll(self):
        try: self.build_progress()
        except Exception as e: self.bartxt.config(text='progress error: %s' % e)
        if getattr(self, 'autolog', None) is not None and self.autolog.get() and self.nb.index(self.nb.select()) == 2: self.refresh_log()
        hb = os.path.join(LIVE, 'heartbeat.txt')
        try:
            age = time.time() - os.path.getmtime(hb); txt = open(hb).read().strip()
            if age < 5: self.status.config(text='RUNNING  ' + txt.split(' time=')[0], bg='#1b6b2a')
            else: self.status.config(text='NOT RESPONDING (no heartbeat for %.0fs) - frozen or closed' % age, bg='#9a1c1c')
        except OSError:
            self.status.config(text='no heartbeat (game not running, or build older than ISS-146)', bg='#444')
        self.after(1000, self.poll)

if __name__ == '__main__': App().mainloop()
