'use strict';
/* SvR06 Launcher UI - vanilla JS, no dependencies. Talks to server.py with a per-session token. */
const $ = (s, r = document) => r.querySelector(s);
const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
const ICON = {
  home: '<path d="M5 3l14 9-14 9z"/>',
  access: '<circle cx="12" cy="4.5" r="2"/><path d="M5 8.5l7 1.5 7-1.5M12 10v5M9 21l3-6 3 6"/>',
  visuals: '<rect x="3" y="4" width="18" height="14" rx="2"/><path d="M8 21h8M12 18v3M7 12l3-3 3 3 2-2 2 2"/>',
  shots: '<path d="M4 8a2 2 0 012-2h2l1.5-2h5L16 6h2a2 2 0 012 2v9a2 2 0 01-2 2H6a2 2 0 01-2-2z"/><circle cx="12" cy="12.5" r="3.5"/>',
  logs: '<path d="M4 5h16M4 10h16M4 15h10M4 20h7"/>',
  tools: '<path d="M14.5 6.5a4 4 0 00-5 5L3 18l3 3 6.5-6.5a4 4 0 005-5l-2.5 2.5-2.5-.5-.5-2.5z"/>',
  health: '<path d="M12 21s-8-4.5-8-11a4.5 4.5 0 018-2.8A4.5 4.5 0 0120 10c0 6.5-8 11-8 11z"/><path d="M8 12h2l1.5-3 2 6 1.5-3h1"/>',
  changes: '<path d="M6 3h9l4 4v14H6z"/><path d="M14 3v5h5M9 13h7M9 17h5"/>',
  settings: '<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 00.3 1.8l.1.1a2 2 0 11-2.8 2.8l-.1-.1a1.7 1.7 0 00-1.8-.3 1.7 1.7 0 00-1 1.5V21a2 2 0 11-4 0v-.1a1.7 1.7 0 00-1.1-1.5 1.7 1.7 0 00-1.8.3l-.1.1a2 2 0 11-2.8-2.8l.1-.1a1.7 1.7 0 00.3-1.8 1.7 1.7 0 00-1.5-1H3a2 2 0 110-4h.1a1.7 1.7 0 001.5-1.1 1.7 1.7 0 00-.3-1.8l-.1-.1a2 2 0 112.8-2.8l.1.1a1.7 1.7 0 001.8.3H9a1.7 1.7 0 001-1.5V3a2 2 0 114 0v.1a1.7 1.7 0 001 1.5 1.7 1.7 0 001.8-.3l.1-.1a2 2 0 112.8 2.8l-.1.1a1.7 1.7 0 00-.3 1.8V9a1.7 1.7 0 001.5 1H21a2 2 0 110 4h-.1a1.7 1.7 0 00-1.5 1z"/>',
  cam: '<path d="M4 8a2 2 0 012-2h2l1.5-2h5L16 6h2a2 2 0 012 2v9a2 2 0 01-2 2H6a2 2 0 01-2-2z"/><circle cx="12" cy="12.5" r="3.5"/>',
  gauge: '<path d="M4 18a8 8 0 1116 0"/><path d="M12 18l4-6"/>',
  bug: '<rect x="8" y="8" width="8" height="11" rx="4"/><path d="M12 8V5M8 12H4M16 12h4M8 16l-3 2M16 16l3 2M9 5l1.5 2M15 5l-1.5 2"/>',
  film: '<rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 4v16M17 4v16M3 9h4M3 15h4M17 9h4M17 15h4"/>',
  trash: '<path d="M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3"/>',
  folder: '<path d="M3 6a2 2 0 012-2h4l2 2h8a2 2 0 012 2v9a2 2 0 01-2 2H5a2 2 0 01-2-2z"/>',
  hammer: '<path d="M14 5l5 5-3 3-5-5zM11 8L3 16l3 3 8-8"/>',
  check: '<path d="M5 12l5 5 9-10"/>',
  cheats: '<path d="M12 3l2.5 5.5L20 9.3l-4 4 1 5.7-5-2.8-5 2.8 1-5.7-4-4 5.5-.8z"/>',
  puzzle: '<path d="M10 4a2 2 0 114 0v2h3a1 1 0 011 1v3h-2a2 2 0 100 4h2v3a1 1 0 01-1 1h-3v-2a2 2 0 10-4 0v2H7a1 1 0 01-1-1v-3H4a2 2 0 110-4h2V7a1 1 0 011-1h3z"/>',
  games: '<rect x="3" y="7" width="18" height="10" rx="4"/><path d="M8 10v4M6 12h4M15 11h.01M17.5 13h.01"/>',
};
ICON.report = ICON.bug;
ICON.about = '<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5h.01"/>';
const svg = n => `<svg viewBox="0 0 24 24">${ICON[n] || ICON.puzzle}</svg>`;
const PAGES = [
  ['home', 'Play'], ['games', 'Games'], ['cheats', 'Cheats'], ['visuals', 'Visuals'], ['access', 'Accessibility'], ['audio', 'Audio'], ['enhance', 'Enhancements'], ['mods', 'Mods'], ['shots', 'Screenshots'], ['logs', 'Logs'],
  ['report', 'Report issue'], ['tools', 'Tools'], ['health', 'Health'], ['changes', 'Changelog'], ['settings', 'Settings'], ['about', 'About & licenses'],
];
const ACCENTS = [['#e11d48', '#f97316'], ['#2563eb', '#06b6d4'], ['#16a34a', '#a3e635'], ['#9333ea', '#ec4899'], ['#f59e0b', '#ef4444'], ['#64748b', '#94a3b8']];

let S = null;            // server state
let page = 'home';
let healthCache = null;
let sigLast = '';
let logState = {src: 'run_err', ver: '', follow: true, busy: false};

/* ---------- api ---------- */
async function api(path, body) {
  const opt = {headers: {'X-Token': window.TOKEN}};
  if (body !== undefined) { opt.method = 'POST'; opt.body = JSON.stringify(body); opt.headers['Content-Type'] = 'application/json'; }
  const r = await fetch(path, opt);
  if (!r.ok) throw new Error(path + ' -> ' + r.status);
  return r.json();
}
const fileUrl = p => '/file?p=' + encodeURIComponent(p) + '&t=' + encodeURIComponent(window.TOKEN);
function toast(msg) {
  const t = document.createElement('div'); t.className = 'toast'; t.textContent = msg; $('#toasts').appendChild(t);
  setTimeout(() => { t.style.opacity = 0; t.style.transition = '.4s'; setTimeout(() => t.remove(), 400); }, 3800);
}
async function act(name, arg) {
  try { const r = await api('/api/action', {name, arg}); toast(r.message); } catch (e) { toast('Failed: ' + e.message); }
  refresh();
}
async function setPref(k, v) {
  S.prefs[k] = v;
  try { const r = await api('/api/prefs', {[k]: v}); S.prefs = r.prefs; S.env = r.env; } catch (e) { toast('Could not save: ' + e.message); }
  if (k === 'accent' || k === 'theme') applyTheme();
  render(true);
}
function applyTheme() {
  const p = S.prefs, pair = ACCENTS.find(a => a[0] === p.accent) || ACCENTS[0];
  const r = document.documentElement; r.style.setProperty('--accent', pair[0]); r.style.setProperty('--accent2', pair[1]); r.dataset.theme = p.theme || 'dark';
}

/* ---------- helpers ---------- */
function age(sec) {
  sec = Math.max(0, Math.floor(sec)); if (sec < 90) return sec + 's'; if (sec < 5400) return Math.round(sec / 60) + ' min';
  if (sec < 172800) return (sec / 3600).toFixed(1) + ' h'; return Math.floor(sec / 86400) + ' d';
}
const iconName = id => (S.icon_sets.find(s => s.id === id) || {name: 'Original (PS2)'}).name;
const sw = (on, key) => `<div class="sw ${on ? 'on' : ''}" data-sw="${key}" role="switch" aria-checked="${!!on}"></div>`;
function statusInfo() {
  const g = S.game, b = S.build;
  if (b.state === 'running') return ['run', `Building ${b.n}/${b.total || '?'}`];
  if (b.state === 'linking') return ['run', 'Linking  -  almost done'];
  const hb = g.heartbeat;
  if (hb && hb.alive) return ['run', 'Game running  -  ' + hb.text.split(' time=')[0].slice(0, 60)];
  if (g.running && hb && !hb.alive) return ['bad', 'Game not responding'];
  if (g.running) return ['run', 'Game running'];
  if (b.state === 'failed') return ['bad', 'Last build failed'];
  return ['', 'Ready'];
}

/* ---------- chrome ---------- */
function renderNav() {
  const failing = healthCache ? healthCache.filter(h => h.ok === false).length : 0;
  $('#nav').innerHTML = PAGES.map(([id, name]) => `<div class="nav-i ${id === page ? 'on' : ''}" data-page="${id}">${svg(id === 'home' ? 'home' : id)}<span>${name}</span>${id === 'health' && failing ? `<span class="badge">${failing}</span>` : ''}</div>`).join('');
}
function renderChrome() {
  $('#ver').textContent = S.version || 'dev';
  $('#rootpath').textContent = S.root;
  const [cls, txt] = statusInfo();
  const p = $('#statuspill'); p.className = 'pill ' + cls; $('#statustext').textContent = txt;
  const b = S.build, bar = $('#buildbar');
  if (b.state === 'running') {
    bar.hidden = false; bar.className = 'buildbar'; $('#bbfill').style.width = (b.total ? 100 * b.n / b.total : 4) + '%';
    $('#bbtext').textContent = `Building  ${b.n}/${b.total || '?'} files`;
  } else if (b.state === 'linking') {
    bar.hidden = false; bar.className = 'buildbar'; $('#bbfill').style.width = '100%'; $('#bbtext').textContent = 'Linking the game - this takes a few minutes, do not close anything';
  } else if (b.state === 'failed') {
    bar.hidden = false; bar.className = 'buildbar fail'; $('#bbfill').style.width = '100%'; $('#bbtext').textContent = 'Last build FAILED - see Logs > Build';
  } else bar.hidden = true;
  $('#topquit').style.display = (S.game.running || (S.game.heartbeat && S.game.heartbeat.alive)) ? '' : 'none';
}

/* ---------- pages ---------- */
function pHome() {
  const p = S.prefs, tp = S.texpack, lr = S.last_run, g = S.game;
  const exeAge = g.exe_mtime ? age(S.now - g.exe_mtime) + ' ago' : 'not built';
  const mode = S.modes.find(m => m.id === p.mode) || S.modes[0];
  const selBuild = (S.builds || []).find(x => x.id === S.sel_build) || {name: 'PAL build', id: 'pal'};
  const sig = S.env.length ? S.env.map(([k, v]) => `<span class="chip">${esc(k.replace('SVR06_', ''))}=${esc(v)}</span>`).join('') : '<span class="chip">defaults</span>';
  return `<div class="page">
  <div class="hero">
    <div class="hero-k">Native PC recompilation</div>
    <div class="hero-t">SmackDown <b>vs</b> Raw 2006</div>
    <div class="hero-s">${esc(selBuild.name)} running on your PC through the PS2 static recompiler and a Vulkan renderer. Pick your options below, then hit play.</div>
    <div class="buildsel"><label for="buildsel">Version</label><select id="buildsel">${(S.builds || []).map(x => `<option value="${x.id}" ${x.id === S.sel_build ? 'selected' : ''} ${x.has_elf && !x.planned ? '' : 'disabled'}>${esc(x.name)} (${esc(x.serial)}) [${esc(x.stage_label || '')}]${x.planned ? '' : '  -  ' + (x.built ? 'built ' + age(S.now - x.exe_mtime) + ' ago' : 'not built yet')}${x.has_elf || x.planned ? '' : '  -  game files missing'}</option>`).join('')}</select></div>
    <div class="buildsel"><label for="cardsel">Memory card</label><select id="cardsel" ${S.game.running ? 'disabled title="Close the game to change the card"' : ''}>${(((S.cards || {}).items) || []).length ? S.cards.items.map(c => `<option value="${esc(c.name)}" ${c.name === S.cards.current ? 'selected' : ''}>${esc(c.name)}  (${c.kb} KB)</option>`).join('') : '<option value="">Default (created on first play)</option>'}</select><button class="btn ghost" data-act="newcard" ${S.game.running ? 'disabled' : ''} title="Creates an empty card and selects it. Your other cards are kept.">New card</button></div>
    <div class="hero-meta" style="margin-top:8px">${stageChip(selBuild)}${(selBuild.tags || []).map(t => `<span class="chip">${esc(t)}</span>`).join('')}</div>
    ${selBuild.note ? `<div class="muted" style="margin-top:8px;max-width:640px">${esc(selBuild.note)}</div>` : ''}
    <div class="hero-act">
      <button class="btn primary" data-act="play">Play</button>
      <button class="btn" data-act="rebuild">${svg('hammer')} Rebuild + play</button>
      <button class="btn ghost" data-act="build">Rebuild only</button>
    </div>
    <div class="hero-meta">
      <span class="chip accent">${esc(S.version || 'dev build')}</span>
      <span class="chip">Mode: ${esc(mode.name)}</span>
      <span class="chip">Icons: ${esc(iconName(p.icons))}</span>
      <span class="chip ${p.texpack ? 'ok' : ''}">HD textures: ${p.texpack ? 'detect' : 'off'}</span>
    </div>
  </div>
  <div class="grid g4 mt">
    <div class="card stat"><div class="k">Game build</div><div class="v" data-live="exeAge">${exeAge}</div><div class="d">${S.build.state === 'ok' ? 'last build finished' : S.build.state === 'failed' ? 'last build failed' : S.build.state === 'interrupted' ? 'last build interrupted' : 'no build log'}</div></div>
    <div class="card stat"><div class="k">Last run</div><div class="v" data-live="lastRun">${lr ? age(S.now - lr.mtime) + ' ago' : 'none'}</div><div class="d">${lr ? (lr.errors ? lr.errors + ' suspicious log lines' : 'no errors flagged') : 'run the game once'}</div></div>
    <div class="card stat"><div class="k">Texture pack</div><div class="v">${tp.count ? tp.count.toLocaleString() : '0'}</div><div class="d">${lr && lr.texpack_on ? lr.exact + ' exact matches last run' : 'files installed'}</div></div>
    <div class="card stat"><div class="k">Button icons</div><div class="v" style="font-size:20px;line-height:34px">${esc(iconName(p.icons))}</div><div class="d">${lr && lr.icon_set ? 'last run loaded: ' + esc(lr.icon_set) : 'last run: original'}</div></div>
  </div>
  <div class="grid g2 mt">
    <div class="card">
      <h3>Quick setup</h3><div class="sub">Saved automatically. Applies the next time you press Play.</div>
      <div class="row"><div><div class="t">Performance mode</div><div class="d">${esc(mode.desc)}</div></div>
        <div class="seg">${S.modes.map(m => `<button class="${m.id === p.mode ? 'on' : ''}" data-mode="${m.id}">${esc(m.name)}</button>`).join('')}</div></div>
      <div class="row"><div><div class="t">Controller button icons</div><div class="d">Currently ${esc(iconName(p.icons))}. Choose in Visuals.</div></div><button class="btn sm" data-go="visuals">Change</button></div>
      <div class="row"><div><div class="t">HD texture pack (detect only)</div><div class="d">Logs which textures match the pack. No visual change yet.</div></div>${sw(p.texpack, 'texpack')}</div>
      <div class="row"><div><div class="t">Boot-freeze workaround</div><div class="d">Keep on. Without it the game can stall before the intro movies (ISS-160).</div></div>${sw(p.probes, 'probes')}</div>
      <div class="row"><div><div class="t">Skip intro movies</div><div class="d">Jump straight to the title screen.</div></div>${sw(p.skipmovies, 'skipmovies')}</div>
      <div class="sub" style="margin:14px 0 6px">Will launch with</div><div class="hero-meta" style="margin:0">${sig}</div>
    </div>
    <div class="card"><h3>Recent changes</h3><div class="sub">From the project changelog</div><div id="homechanges"><div class="muted">loading...</div></div></div>
  </div></div>`;
}
function previewBox(id) {
  const url = fileUrl(id ? `mods/icons/${id}/glyph_sheet.png` : 'mods/icons/orig_sheet.png');
  return `<div class="pv"><div class="crop" style="background-image:url('${url}')"></div></div>`;
}
const GS_OPTS = [
  {k: 'graphics.internalRes', n: 'Internal resolution', d: 'Renders the 3D picture with more samples on the GPU. Native is the original look and the fastest; higher costs GPU time.', opts: ['1x native', '1x + SSAA 4x', '2x', '2x + SSAA 8x']},
  {k: 'graphics.aspect', n: 'Aspect ratio', d: 'Picture shape. Wider modes also widen the 3D camera.', opts: ['4:3', '16:9', '21:9', 'Stretch']},
  {k: 'graphics.filter', n: 'Texture filter', d: 'Nearest keeps the PS2 look; bilinear smooths the final image.', opts: ['Nearest', 'Bilinear']},
  {k: 'graphics.vsync', n: 'Vsync', d: 'Syncs presentation with the monitor.', opts: ['Off', 'On']},
  {k: 'graphics.showFps', n: 'Show FPS counter', d: 'Frame counter in the top left (F3 in game).', opts: ['Off', 'On']},
  {k: 'access.colorblind', acc: 1, n: 'color blind mode', d: 'Corrects the game picture so confusable colors are easier to tell apart.', opts: ['Off', 'Protanopia', 'Deuteranopia', 'Tritanopia']},
  {k: 'menu.highContrast', acc: 1, n: 'high-contrast menu', d: 'Brighter F1 menu text on a fully opaque background.', opts: ['Off', 'On']},
  {k: 'menu.boldText', acc: 1, n: 'bold menu text', d: 'Thicker F1 menu text for reading at a distance (Atkinson Hyperlegible font).', opts: ['Off', 'On']},
  {k: 'menu.reduceMotion', acc: 1, n: 'reduce menu motion', d: 'Turns off F1 menu fade and slide animations.', opts: ['Off', 'On']},
  {k: 'input.p1.mashAssist', acc: 1, n: 'mash assist (Player 1)', d: 'Hold Cross to auto-repeat it for pin and submission escapes.', opts: ['Off', 'On']},
  {k: 'input.p2.mashAssist', acc: 1, n: 'mash assist (Player 2)', d: 'Same for Player 2.', opts: ['Off', 'On']},
  {k: 'input.p1.sticky', acc: 1, n: 'sticky shoulder buttons (Player 1)', d: 'L1/R1/L2/R2 toggle on press instead of needing to be held.', opts: ['Off', 'On']},
  {k: 'graphics.renderer', n: 'Renderer', d: 'Vulkan is the player renderer; Software is the slow CPU reference renderer for testing.', opts: ['Vulkan (paraLLEl-GS)', 'Software']},
];
function gsVal(k, def) { const g = S.gset || {}; return k in g ? g[k] : def; }
function gameSettingsCard(accOnly) {
  const meta = S.gset_meta || {}, run = S.game && S.game.running;
  const defs = {'graphics.vsync': 1, 'graphics.showFps': 1, 'graphics.aspect': 0};
  return `<div class="card"><h3>${accOnly ? 'Accessibility' : 'Game settings'}</h3><div class="sub">The same settings as the in-game F1 menu. Both read and write one file, so a change in either shows up in the other${run ? ' while the game is running' : ''}. <b>Restart</b> marks settings the game only reads at startup.</div>${GS_OPTS.filter(o => accOnly ? o.acc : !o.acc).map(o => {
    const v = gsVal(o.k, defs[o.k] || 0), rs = meta[o.k] && meta[o.k].restart;
    return `<div class="row"><div><div class="t">${esc(o.n)} ${rs ? '<span class="chip warn">Restart game to apply</span>' : '<span class="chip ok">Applies live</span>'}</div><div class="d">${esc(o.d)}</div></div><div class="seg">${o.opts.map((x, i) => `<button class="${v === i ? 'on' : ''}" data-gset="${o.k}" data-val="${i}">${esc(x)}</button>`).join('')}</div></div>`;
  }).join('')}</div>`;
}
const GS_CHEAT_IDS = ['unlock_all', 'free_shop'];
const CHEAT_APPLY = {exh_open_titles: 'Applies live - reopen the match setup screen', god_mode: 'Applies live during a match', finisher_start: 'Applies live - takes effect when the next match starts', finisher_always: 'Applies live during a match', auto_reversal: 'Applies live during a match', gm_: 'Applies live - re-enter the GM screen to refresh', unlock_npc: 'Applies live - leave and re-enter character select', unlock_: 'Applies live - re-enter the shop screen to see it', free_shop: 'Applies live - re-enter the shop screen to see it'};
function cheatApply(c) { const k = Object.keys(CHEAT_APPLY).find(p => c.id === p || (p.endsWith('_') && c.id.startsWith(p))); return k ? CHEAT_APPLY[k] : 'Applies live'; }
function pAccess() {
  return `<div class="page">${gameSettingsCard(true)}<div class="card mt"><h3>More to come</h3><div class="sub">These are the same options as the F1 menu's Accessibility page. Color blind mode corrects the game picture; the menu options only change the F1 menu.</div></div></div>`;
}
function pVisuals() {
  const p = S.prefs, tp = S.texpack, lr = S.last_run;
  const sel = S.icon_sets.find(s => s.id === p.icons) || S.icon_sets[0];
  return `<div class="page">
  ${gameSettingsCard()}
  <div class="card mt"><h3>Controller button icons</h3><div class="sub">Replaces the Cross / Square / Triangle / Circle, shoulder buttons and d-pad prompts in menus. Takes effect on the next launch.</div>
    <div class="icons">${S.icon_sets.map(s => `<div class="ic ${s.id === p.icons ? 'on' : ''} ${s.ready ? '' : 'na'}" data-icon="${esc(s.id)}">
      <div class="tick"><svg viewBox="0 0 24 24">${ICON.check}</svg></div>${previewBox(s.id)}<div class="nm">${esc(s.name)}</div><div class="sb">${esc(s.sub)}${s.ready ? '' : ' - not built'}</div></div>`).join('')}</div></div>
  <div class="grid g2 mt">
    <div class="card"><h3>Full sheet preview</h3><div class="sub">${esc(sel.name)} - the 128x256 texture the game uses for every button prompt.</div>
      <div class="sheet-modal"><img class="sheetimg" src="${fileUrl(sel.id ? `mods/icons/${sel.id}/glyph_sheet.png` : 'mods/icons/orig_sheet.png')}" alt="">
      <div class="muted" style="font-size:13px">Face buttons follow their physical position, so the bottom button is Cross on PlayStation, A on Xbox and B on Switch.<br><br>Want your own? Edit <span class="mono">mods/icons/my_sheet.png</span> and run <span class="mono">tools/make_icon_pack.py</span>, then pick <b>Custom pack</b>.</div></div></div>
    <div class="card"><h3>HD texture pack</h3><div class="sub">Emulation Lovers HD pack (PCSX2 format)</div>
      <div class="row"><div><div class="t">Detect pack matches</div><div class="d">Hashes game textures like PCSX2 and logs which ones the pack covers.</div></div>${sw(p.texpack, 'texpack')}</div>
      <div class="kv" style="margin-top:12px"><div>Files installed</div><div>${tp.count.toLocaleString()}</div><div>Location</div><div>${esc(tp.path)}</div>
      <div>Last run</div><div>${lr && lr.texpack_on ? `${lr.exact} exact matches` + (lr.seen ? ` of ${lr.seen} textures seen` : '') : 'detection was off'}</div>
      <div>Stage</div><div>1 of 3 - detect. Next: display HD textures.</div></div>
      <div style="margin-top:14px"><button class="btn sm" data-act="open" data-arg="texpack">${svg('folder')} Open folder</button> <button class="btn sm" data-go="logs" data-filter="texpack">View matches in Logs</button></div></div>
  </div></div>`;
}
const STAGE_CLS = {planned: '', recompiled: 'bad', bootable: 'warn', menus: 'warn', ingame: 'accent', playable: 'ok'};
function stageChip(b) { return b ? `<span class="chip ${STAGE_CLS[b.stage] || ''}">${esc(b.stage_label || b.stage)}</span>` : ''; }
function buildBySerial(ser) { const n = s => String(s || '').replace(/[^A-Za-z0-9]/g, '').toUpperCase(); return (S.builds || []).find(b => n(b.serial) === n(ser)); }
function gameCard(d, kind) {
  const ps2 = kind === 'ps2', prim = !ps2 && S.library.psp_primary === d.slot_id;
  let st = ['Not linked', ''];
  if (d.found) st = ps2 ? (d.supported ? (() => { const b = buildBySerial(d.serial); return b ? [b.stage_label || 'Linked', {planned: '', recompiled: 'bad', bootable: 'warn', menus: 'warn', ingame: 'accent', playable: 'ok'}[b.stage] || 'warn'] : ['Linked', 'warn']; })() : ['Linked', 'warn']) : ['Linked', 'ok'];
  else if (d.path) st = ['Not recognised', 'bad'];
  const id = ps2 ? (d.serial || '') : (d.disc_id || '');
  const feats = !ps2 && d.found && d.features && d.features.length ? `<div class="feats">${d.features.map(f => `<div class="ft ${f.ok ? 'ok' : ''}"><span class="fi">${f.ok ? '&#10003;' : '&#8211;'}</span><div><div class="t">${esc(f.name)}</div><div class="d">${esc(f.what)} (${esc(f.detail)})</div></div></div>`).join('')}</div>` : '';
  return `<div class="card game ${d.found ? '' : 'unlinked'}">
    <div class="gh"><div><div class="gt">${esc(d.name)}</div><div class="d">${esc(d.sub || '')}</div></div><div class="gch"><span class="chip">${esc(d.region)}</span>${prim ? '<span class="chip accent">PSP data source</span>' : ''}<span class="chip ${st[1]}">${st[0]}</span></div></div>
    ${d.found ? `<div class="kv" style="margin:10px 0 4px"><div>Id</div><div>${esc(id || d.kind || '-')}${d.title ? ' - ' + esc(d.title) : ''}</div><div>Type</div><div>${esc(d.kind || '')}${d.auto ? ' (found automatically)' : ''}</div></div><div class="d" style="margin:6px 0">${esc(d.note || '')}</div>` : (d.path ? `<div class="d" style="margin:8px 0;color:var(--bad)">${esc(d.note || '')}</div>` : '')}
    ${feats}
    <div class="gpath"><input class="in" data-gpath="${esc(d.slot_id)}" placeholder="${ps2 ? 'Folder or disc image of the game' : 'Extracted PSP folder (contains PSP_GAME)'}" value="${esc(d.path || '')}">
      <button class="btn sm" data-gbrowse="${esc(d.slot_id)}">${svg('folder')} Folder</button>${ps2 ? `<button class="btn sm" data-gbrowse="${esc(d.slot_id)}" data-gmode="file">${svg('film')} Disc image</button>` : ''}<button class="btn sm primary" data-glink="${esc(d.slot_id)}">Link</button>
      ${d.linked ? `<button class="btn sm ghost" data-gunlink="${esc(d.slot_id)}">Unlink</button>` : ''}
      ${!ps2 && d.found && d.features && d.features.length && !prim ? `<button class="btn sm" data-gprim="${esc(d.slot_id)}">Use for extras</button>` : ''}</div>
  </div>`;
}
function pGames() {
  const L = S.library, p = S.prefs;
  const prim = L.psp.find(d => d.slot_id === L.psp_primary);
  const arc = prim && prim.features ? prim.features.filter(f => ['gameshow', 'eugene', 'holdem'].includes(f.id)) : [];
  const arcOk = arc.some(f => f.ok);
  return `<div class="page">
  <div class="card"><h3>Game versions</h3><div class="sub">Link every copy you own. Folders are read in place; nothing is copied, changed or uploaded. The launcher finds anything inside the <span class="mono">assets</span> folder by itself.</div>
    <div class="hero-meta" style="margin:0"><button class="btn sm" data-gscan="1">Rescan</button><span class="muted" style="font-size:12.5px">${L.ps2.filter(d => d.found).length} PS2 and ${L.psp.filter(d => d.found).length} PSP versions found</span></div></div>
  <div class="gsec">PlayStation 2</div><div class="grid g2">${L.ps2.map(d => gameCard(d, 'ps2')).join('')}</div>
  <div class="gsec">PlayStation Portable <span class="muted">- for the PSP-only features</span></div>
  <div class="card"><div class="row"><div><div class="t">Enable PSP extras (Arcade Mode)</div><div class="d">${arcOk ? 'Data found in ' + esc(prim.name) + '. Without PSP data the Arcade Mode menu entry stays greyed out, like Online.' : 'Needs a linked PSP version with the Arcade data. Until then Arcade Mode stays greyed out, like Online.'} The game side of Arcade Mode is still being built; this switch only passes the data location to it.</div></div>${sw(p.arcade && arcOk, 'arcade')}</div></div>
  <div class="grid g2 mt">${L.psp.map(d => gameCard(d, 'psp')).join('')}</div></div>`;
}
const STATUS = {working: ['Working', 'ok'], untested: ['Untested', 'warn'], todo: ['Not mapped yet', '']};
function cheatBid() { return S.sel_build || 'pal'; }
function cheatStore() { const c = (S.prefs.cheats || {})[cheatBid()]; return c && typeof c === 'object' ? c : {}; }
function cheatOk() { return !!(S.cheats.verified || {})[cheatBid()]; }
function cheatSet(key, val) { const all = Object.assign({}, S.prefs.cheats || {}), b = cheatBid(); all[b] = Object.assign({}, all[b] || {}, {[key]: val}); return setPref('cheats', all); }
function cheatCtl(c, who) {
  const key = who ? c.id + '.' + who : c.id, v = cheatStore()[key];
  const dis = c.status === 'todo' || !cheatOk();
  if (c.kind === 'choice') return `<div class="seg ${dis ? 'dis' : ''}">${c.opts.map(o => `<button class="${(v || 'off') === o ? 'on' : ''}" ${dis ? 'disabled' : ''} data-cheat="${key}" data-val="${o}">${o === 'unlimited' ? 'Unlimited' : o === 'off' ? 'Off' : o}</button>`).join('')}</div>`;
  if (c.kind === 'checks') {
    const sel = Array.isArray(v) ? v : [];
    return `<div class="chkwrap ${dis ? 'dis' : ''}"><span class="chip">${sel.length}/${c.items.length} ticked</span><button class="btn sm" data-chkall="${c.id}:1">Select all</button><button class="btn sm" data-chkall="${c.id}:0">Clear</button></div>`;
  }
  return `<div class="${dis ? 'dis' : ''}">${sw(!!v, 'cheat:' + key)}</div>`;
}
function chkGrid(c) {
  const v = cheatStore()[c.id], sel = Array.isArray(v) ? v : [], dis = c.status === 'todo' || !cheatOk();
  return `<div class="chkgrid ${dis ? 'dis' : ''}">${c.items.map(([n, nm]) => `<label class="chkrow"><input type="checkbox" ${sel.includes(n) ? 'checked' : ''} ${dis ? 'disabled' : ''} data-chkitem="${c.id}:${n}"><span>${esc(nm)}</span></label>`).join('')}</div>`;
}
function pCheats() {
  const defs = S.cheats.defs, groups = [...new Set(defs.map(c => c.g))];
  const run = S.game.heartbeat && S.game.heartbeat.alive;
  const nWork = defs.filter(c => c.status !== 'todo').length, sb = (S.builds || []).find(x => x.id === cheatBid()), ok = cheatOk();
  return `<div class="page">
  <div class="card"><h3>Cheats</h3><div class="sub">Written into the running game through its live console, so no rebuild and no files are changed. They apply while the game is open and stop when it closes.</div>
    <div class="hero-meta" style="margin:0"><span class="chip ${run ? 'ok' : ''}">${run ? 'Game is running' : 'Start the game to apply'}</span><span class="chip accent">Cheats for: ${esc(sb ? sb.name : cheatBid().toUpperCase())}</span><span class="chip ${ok ? 'ok' : 'warn'}">${ok ? nWork + ' of ' + defs.length + ' mapped' : 'not mapped for this version yet'}</span>${S.cheats.msg ? `<span class="chip accent">${esc(S.cheats.msg)}</span>` : ''}</div>
    <div class="d" style="margin-top:10px;color:var(--muted);font-size:12.5px">Items marked <b>Not mapped yet</b> still need their memory addresses found in the game; they are listed so nothing from the wish list gets lost. Unlocks change the save data in memory, so save in the game afterwards if you want to keep them.</div></div>
  ${groups.map(g => `<div class="gsec">${esc(g)}</div><div class="card cheats">${defs.filter(c => c.g === g).map(c => {
    const st = STATUS[c.status];
    const ctl = c.per ? `<div class="pl"><span class="who">P1</span>${cheatCtl(c, 'p1')}<span class="who">P2</span>${cheatCtl(c, 'p2')}</div>` : cheatCtl(c, '');
    return `<div class="row ch ${c.status === 'todo' ? 'todo' : ''}"><div><div class="t">${esc(c.name)} <span class="chip ${st[1]}">${st[0]}</span>${c.per ? ' <span class="chip">per player</span>' : ''}</div><div class="d" style="opacity:.75">${esc(cheatApply(c))}${GS_CHEAT_IDS.includes(c.id) ? ' (also in the F1 menu)' : ''}</div><div class="d">${esc(c.desc)}</div></div>${ctl}</div>${c.kind === 'checks' ? chkGrid(c) : ''}`;
  }).join('')}</div>`).join('')}</div>`;
}
const PLANNED = {
  audio: ['Audio', 'Settings that need support inside the game runtime. None of these exist yet.', [
    ['Surround sound output', 'Output 5.1 from the game audio instead of stereo.'], ['Stereo options', 'Width, mono, balance.'],
    ['Separate volume sliders', 'Commentary, sound effects, entrance music, menu music and crowd.']]],
  enhance: ['Enhancements', 'Features that change how the game plays.', [
    ['Rewind (10 to 30 seconds)', 'Needs full save states and a ring buffer of them. Only possible if save states work; checked first.'],
    ['Fast forward', 'Run the game faster than 60 frames per second. Depends on how fast the recompiled game is on your PC.'],
    ['Save / load states', 'Snapshot RAM, graphics memory and audio state to a file.']]],
  mods: ['Mods', 'Importing and replacing game content. The original archives are never modified; replacements load from a separate folder.', [
    ['Music', 'Entrance and menu music replacements.'], ['Titantron videos', 'Replace entrance videos.'], ['Models and attire', 'Wrestler models and costumes.'],
    ['Venues', 'Arenas and stage sets.'], ['Move sets', 'Moves and animations.'], ['Textures', 'HD texture packs (detection works; display is the next stage).']]],
};
function pPlanned(id) {
  const [t, sub, items] = PLANNED[id];
  return `<div class="page"><div class="card"><h3>${esc(t)}</h3><div class="sub">${esc(sub)}</div>${items.map(([n, d]) => `<div class="row"><div><div class="t">${esc(n)} <span class="chip">Planned</span></div><div class="d">${esc(d)}</div></div></div>`).join('')}</div></div>`;
}
function pShots() {
  return `<div class="page"><div class="logbar"><button class="btn primary sm" data-act="shot">${svg('cam')} Take screenshot now</button><button class="btn sm" data-act="open" data-arg="shots">Open folder</button><span class="muted" id="shotcount"></span></div><div class="shots" id="shotgrid"><div class="muted">loading...</div></div></div>`;
}
function pLogs() {
  const chips = [['All', ''], ['Textures', 'texpack'], ['Icons', 'iconhook'], ['Errors', 'error|fatal|exception|assert|crash|FAILED'], ['Warnings', 'warn']];
  const f = S.prefs.logfilter || '';
  const vers = (S.builds || []).filter(b => !b.planned);
  if (!logState.ver || !vers.some(b => b.id === logState.ver)) logState.ver = (S.prefs && S.prefs.last_launched) || S.sel_build || 'pal';
  return `<div class="page"><div class="logbar">
    <div class="seg">${vers.map(b => `<button class="${logState.ver === b.id ? 'on' : ''}" data-lver="${b.id}">${esc(b.name.replace(/ \(.*\)$/, ''))}</button>`).join('')}</div>
    <div class="seg">${[['run_err', 'Game log'], ['run_out', 'Console'], ['build', 'Build']].map(([i, n]) => `<button class="${logState.src === i ? 'on' : ''}" data-src="${i}">${n}</button>`).join('')}</div>
    <input class="in" id="logfilter" placeholder="filter (use | for OR)" value="${esc(f)}">
    ${chips.map(([n, v]) => `<span class="chip ${f === v ? 'accent' : ''}" style="cursor:pointer" data-chip="${esc(v)}">${n}</span>`).join('')}
    <label class="muted" style="margin-left:auto;display:flex;gap:8px;align-items:center">Follow ${sw(logState.follow, 'follow')}</label></div>
    <div class="logbox" id="logbox"></div><div class="muted" id="logmeta" style="margin-top:8px"></div></div>`;
}
function pTools() {
  const tool = (ic, t, d, attrs) => `<button class="tool" ${attrs}>${svg(ic)}<div class="t">${t}</div><div class="d">${d}</div></button>`;
  return `<div class="page">
  <div class="card"><h3>In the game</h3><div class="sub">Needs the game to be running.</div><div class="grid g3">
    ${tool('cam', 'Screenshot', 'Save the current frame to the gallery', 'data-act="shot"')}
    ${tool('gauge', 'Performance snapshot', 'Frame times and subsystem timings in the toast', 'data-act="perf"')}
    ${tool('film', 'Quit game', 'Close it cleanly, force it if it hangs', 'data-act="quit"')}</div></div>
  <div class="card mt"><h3>Support</h3><div class="sub">When something goes wrong.</div><div class="grid g3">
    ${tool('bug', 'Collect bug report', 'Zip of logs and the latest screenshots (never game files)', 'data-act="report"')}
    ${tool('trash', 'Clean old logs', 'Keep only the newest 20 saved runs', 'data-act="prune"')}
    ${tool('hammer', 'Rebuild only', 'Compile without starting the game', 'data-act="build"')}</div></div>
  <div class="card mt"><h3>Folders</h3><div class="sub">Open in Explorer.</div><div class="grid g4">
    ${tool('folder', 'Project', 'Everything', 'data-act="open" data-arg="root"')}${tool('folder', 'Logs', 'analysis', 'data-act="open" data-arg="logs"')}
    ${tool('folder', 'Icon sets', 'mods/icons', 'data-act="open" data-arg="icons"')}${tool('folder', 'Texture pack', 'mods/texturepack', 'data-act="open" data-arg="texpack"')}</div></div>
  ${S.scenarios.length ? `<div class="card mt"><h3>Test scenarios</h3><div class="sub">Scripted button sequences for repeatable testing.</div><div class="hero-meta" style="margin:0">${S.scenarios.map(n => `<button class="btn sm" data-act="scenario" data-arg="${esc(n)}">${esc(n.replace(/\.txt$/, ''))}</button>`).join('')}</div></div>` : ''}</div>`;
}
function pHealth() {
  return `<div class="page"><div class="card"><h3>Health check</h3><div class="sub" id="hsum">checking...</div><div id="hlist"></div><div style="margin-top:12px"><button class="btn sm" data-act="rehealth">Re-check</button></div></div></div>`;
}
function pChanges() {
  return `<div class="page"><div class="logbar"><input class="in" id="clq" placeholder="search the changelog" style="min-width:320px"><button class="btn sm" data-act="open" data-arg="changelog">Open full file</button></div><div class="card" id="cllist"><div class="muted">loading...</div></div></div>`;
}
function pSettings() {
  const p = S.prefs;
  return `<div class="page"><div class="grid g2">
  <div class="card"><h3>Appearance</h3><div class="sub">Accent colour and theme.</div>
    <div class="sw-row">${ACCENTS.map(a => `<div class="swatch ${p.accent === a[0] ? 'on' : ''}" data-accent="${a[0]}" style="background:linear-gradient(135deg,${a[0]},${a[1]})"></div>`).join('')}</div>
    <div class="row" style="margin-top:14px"><div><div class="t">Theme</div></div><div class="seg">${['dark', 'oled', 'light'].map(t => `<button class="${(p.theme || 'dark') === t ? 'on' : ''}" data-theme-set="${t}">${t[0].toUpperCase() + t.slice(1)}</button>`).join('')}</div></div>
    <div class="row"><div><div class="t">Close launcher with its window</div><div class="d">Off keeps the background service alive.</div></div>${sw(p.autoquit, 'autoquit')}</div></div>
  <div class="card"><h3>Memory card format</h3><div class="sub">How saves are stored. Choose once; you can switch later.</div>
    <div class="row" style="flex-direction:column;align-items:flex-start"><div><div class="t">Format</div><div class="d">Folder: each card is a normal folder on this PC (the launcher's card picker uses this). PCSX2 file: a single .ps2 card image you pick, so saves can be shared with PCSX2.</div></div><div class="seg" style="flex:none;align-self:flex-start">${[['folder', 'Folder (native)'], ['pcsx2', 'PCSX2 card file']].map(([v, n]) => `<button class="${(p.card_format || 'folder') === v ? 'on' : ''}" data-pref="card_format" data-val="${v}">${n}</button>`).join('')}</div></div>
    ${(p.card_format || 'folder') === 'pcsx2' ? `<div class="fld"><label>PCSX2 card file (.ps2)</label><input class="in wide" id="card_file" value="${esc(p.card_file || '')}" placeholder="C:\\Users\\you\\Documents\\PCSX2\\memcards\\Mcd001.ps2"></div>
    <div class="d" style="margin-top:8px;color:var(--muted)">Heads up: the game itself cannot read .ps2 card files yet, so this choice is saved but Play still uses the folder card. Your .ps2 file is never touched. Runtime support is planned (it will also back up the file before every write).</div>` : '<div class="d" style="margin-top:8px;color:var(--muted)">Cards live in the game folder under cards/. Nothing is ever deleted when you switch or create cards.</div>'}</div>
  <div class="card"><h3>GitHub</h3><div class="sub">Lets the Report page open and close tickets. The token stays on this PC (config/github_token.txt) and is never uploaded. Create one at github.com/settings/tokens with the <b>repo</b> (or Issues: read/write) permission - or sign in once with <code>gh auth login</code> and leave this empty.</div>
    <div class="fld"><label>Repository</label><input class="in wide" id="gh_repo" value="${esc(p.gh_repo || '')}"></div>
    <div class="fld"><label>Access token</label><input class="in wide" id="gh_token" type="password" placeholder="paste a token (stored locally)"></div>
    <div id="gh_state" style="margin-top:10px">${ghStateHtml()}</div>
    <div style="display:flex;gap:10px;margin-top:10px"><button class="btn sm" data-tk="savetoken">Save token</button><button class="btn sm" data-tk="saverepo">Save repository</button><button class="btn sm" data-tk="testgh">Test connection</button></div></div>
  <div class="card"><h3>About</h3><div class="sub">Where things live.</div><div class="kv"><div>Version</div><div>${esc(S.version || 'dev')}</div><div>Project</div><div>${esc(S.root)}</div><div>Platform</div><div>${esc(S.platform)}</div><div>Saved prefs</div><div>config/launcher_prefs.json</div></div>
    <div style="margin-top:16px;display:flex;gap:10px"><button class="btn sm" data-act="resetprefs">Reset all options</button><button class="btn sm" data-act="exitlauncher">Exit launcher</button></div></div></div></div>`;
}
/* ---------- About & licenses ---------- */
const LICENSES = [
  ['PS2Recomp / ps2xRuntime', 'GNU GPL v3', 'Static recompiler and runtime this port is built on.', 'https://github.com/ran-j/PS2Recomp'],
  ['paraLLEl-GS', 'GNU LGPL v3', 'Vulkan Graphics Synthesizer renderer (with Granite, MIT).', 'https://github.com/themaister/parallel-gs'],
  ['Granite', 'MIT', 'Vulkan rendering backend used by paraLLEl-GS.', 'https://github.com/Themaister/Granite'],
  ['raylib', 'zlib', 'Window, input and audio output.', 'https://github.com/raysan5/raylib'],
  ['Dear ImGui / rlImGui', 'MIT', 'Debug overlay.', 'https://github.com/ocornut/imgui'],
  ['xxHash', 'BSD-2-Clause', 'Fast hashing (texture pack matching).', 'https://github.com/Cyan4973/xxHash'],
  ['Ghidra + Emotion Engine Reloaded', 'Apache 2.0', 'Analysis tools only; not part of the shipped game build.', 'https://github.com/NationalSecurityAgency/ghidra'],
  ['Python, PyInstaller', 'PSF / GPL with bootloader exception', 'Launcher runtime and the packaged launcher exe.', 'https://www.pyinstaller.org'],
];
function pAbout() {
  return `<div class="page"><div class="grid g2">
  <div class="card"><h3>About</h3><div class="sub">SvR06 Recomp launcher - an unofficial fan project that runs WWE SmackDown! vs. Raw 2006 natively on PC by statically recompiling the game code.</div>
    <div class="kv"><div>Launcher version</div><div>${esc(S.version || 'dev')}</div><div>Project folder</div><div>${esc(S.root)}</div><div>Source</div><div>github.com/${esc((S.prefs && S.prefs.gh_repo) || 'jfussell94/svr06-recomp')}</div></div>
    <div class="d" style="margin-top:12px;color:var(--muted);font-size:12.5px">No game data is included or distributed. You must supply your own legally obtained copy of the game; the launcher never modifies your original files. WWE, SmackDown! vs. Raw, THQ, Yuke's and all related names, logos and game assets are the property of their respective owners. This project is not affiliated with or endorsed by any of them.</div></div>
  <div class="card"><h3>Version status</h3><div class="sub">What each stage means. "Playable" is reserved for the 1.0 release.</div>
    ${(S.states || []).map(t => `<div class="row"><div><div class="t"><span class="chip ${STAGE_CLS[t.id] || ''}">${esc(t.label)}</span></div><div class="d">${esc(t.desc)}</div></div></div>`).join('')}</div>
  <div class="card"><h3>Third-party software</h3><div class="sub">Open-source components used to build and run this port. Full license texts are in the repository (LICENSE files in each component and THIRD_PARTY_NOTICES).</div>
    ${LICENSES.map(l => `<div class="row"><div><div class="t">${esc(l[0])} <span class="chip">${esc(l[1])}</span></div><div class="d">${esc(l[2])} <a href="${l[3]}" target="_blank" rel="noopener">${esc(l[3].replace('https://', ''))}</a></div></div></div>`).join('')}</div>
  </div></div>`;
}
/* ---------- GitHub connection status (Settings) ---------- */
const GHS = {st: null, test: null, loading: false};
function ghStateHtml() {
  if (!GHS.st && !GHS.loading) { GHS.loading = true; api('/api/tickets', {op: 'status'}).then(s => { GHS.st = s; GHS.loading = false; render(true); }).catch(() => { GHS.loading = false; }); }
  const s = GHS.st; if (!s) return '<span class="chip">checking...</span>';
  const src = {file: 'saved on this PC', env: 'from environment variable', 'gh cli': 'from gh CLI sign-in'}[s.source] || '';
  const t = GHS.test ? ` <span class="chip ${GHS.test.ok ? 'ok' : 'warn'}">${GHS.test.ok ? 'Connection OK' : 'Test failed'}: ${esc(GHS.test.text)}</span>` : '';
  return (s.has_token ? `<span class="chip ok">Token saved${src ? ' (' + src + ')' : ''}</span>` : '<span class="chip warn">No token saved</span>') + ` <span class="chip">Repository: ${esc(s.repo || 'not set')}</span>` + t;
}
/* ---------- report an issue (GitHub tickets) ---------- */
const RPT = {title: '', what: '', steps: '', area: 'other', region: '', logs: true, list: null, err: '', busy: false, status: null};
const AREAS = [['boot', 'Boot / black screen'], ['graphics', 'Graphics'], ['audio', 'Audio'], ['controls', 'Controls'], ['menus', 'Menus'], ['gameplay', 'Match gameplay'], ['launcher', 'Launcher'], ['other', 'Other']];
async function loadTickets(force) {
  if (RPT.busy && !force) return;
  RPT.busy = true;
  try {
    RPT.status = await api('/api/tickets', {op: 'status'});
    if (RPT.status.has_token) { const r = await api('/api/tickets', {op: 'list'}); RPT.err = r.error || ''; RPT.list = r.tickets || RPT.list; }
  } catch (e) { RPT.err = e.message; }
  RPT.busy = false;
  if (page === 'report') { const el = $('#tklist'); if (el) el.innerHTML = ticketListHtml(); const st = $('#tkstatus'); if (st) st.innerHTML = tkStatusHtml(); }
}
function tkStatusHtml() {
  const st = RPT.status; if (!st) return 'checking...';
  return st.has_token ? 'Connected to <b>' + esc(st.repo) + '</b>' + (st.source ? ' (' + esc(st.source) + ')' : '') : 'No GitHub token yet. Add one in Settings > GitHub, or sign in with <code>gh auth login</code>.';
}
function ticketListHtml() {
  if (!RPT.status || !RPT.status.has_token) return '<div class="muted">Connect GitHub to see your tickets.</div>';
  if (RPT.err) return '<div class="muted">' + esc(RPT.err) + '</div>';
  if (!RPT.list) return '<div class="muted">loading...</div>';
  if (!RPT.list.length) return '<div class="muted">No tickets opened from the launcher yet.</div>';
  return RPT.list.map(t => `<div class="tk ${t.verify ? 'verify' : ''}"><div class="tk-h"><a href="${esc(t.url)}" target="_blank" rel="noopener">#${t.number} ${esc(t.title)}</a><span class="chip ${t.state === 'closed' ? 'ok' : ''}">${t.verify ? 'fix ready - please confirm' : t.state}</span></div>
    <div class="muted">${t.labels.filter(l => /^(region|area):|still-broken/.test(l)).map(esc).join(' · ')}${t.comments ? ' · ' + t.comments + ' comment' + (t.comments > 1 ? 's' : '') : ''}</div>
    ${t.state === 'open' && t.verify ? `<div style="margin-top:8px;display:flex;gap:8px"><button class="btn sm primary" data-tk="confirm" data-n="${t.number}">Confirmed fixed - close</button><button class="btn sm" data-tk="reopen" data-n="${t.number}">Still broken</button></div>` : ''}
    ${t.state === 'closed' ? `<div style="margin-top:8px"><button class="btn sm ghost" data-tk="reopen" data-n="${t.number}">Reopen</button></div>` : ''}</div>`).join('');
}
function pReport() {
  if (!RPT.region) RPT.region = S.sel_build || 'pal';
  return `<div class="page"><div class="grid g2">
  <div class="card"><h3>Report an issue</h3><div class="sub">Goes straight to the project's GitHub issues, with your version and recent log lines attached.</div>
    <div class="fld"><label>Title</label><input class="in wide" id="rp_title" maxlength="140" placeholder="Short summary, e.g. Black screen after the Smackdown logo" value="${esc(RPT.title)}"></div>
    <div class="fld two"><div><label>Game version</label><select id="rp_region" class="sel">${(S.builds || []).map(b => `<option value="${b.id}" ${b.id === RPT.region ? 'selected' : ''}>${esc(b.name.replace(' (planned)', ''))}</option>`).join('')}</select></div>
      <div><label>Area</label><select id="rp_area" class="sel">${AREAS.map(a => `<option value="${a[0]}" ${a[0] === RPT.area ? 'selected' : ''}>${a[1]}</option>`).join('')}</select></div></div>
    <div class="fld"><label>What happened, and what did you expect?</label><textarea class="in wide" id="rp_what" rows="4">${esc(RPT.what)}</textarea></div>
    <div class="fld"><label>Steps to reproduce (optional)</label><textarea class="in wide" id="rp_steps" rows="3">${esc(RPT.steps)}</textarea></div>
    <div class="row"><div><div class="t">Attach recent log lines</div><div class="d">Last lines of the game log, noise filtered out. No game files.</div></div>${sw(RPT.logs, 'rp_logs')}</div>
    <div style="margin-top:14px;display:flex;gap:10px;align-items:center"><button class="btn primary" data-tk="submit">Submit ticket</button><span class="muted" id="tkstatus">${tkStatusHtml()}</span></div>
    <div class="muted" style="margin-top:10px">Screenshots can't be attached from here: open the ticket afterwards and drag the picture in (Screenshots page > Open folder).</div></div>
  <div class="card"><h3>Your tickets</h3><div class="sub">When a fix is pushed the ticket asks you to confirm. Confirming closes it.</div><div id="tklist">${ticketListHtml()}</div>
    <div style="margin-top:12px"><button class="btn sm" data-tk="refresh">Refresh</button></div></div></div></div>`;
}
document.addEventListener('input', e => {
  const m = {rp_title: 'title', rp_what: 'what', rp_steps: 'steps'};
  if (m[e.target.id]) RPT[m[e.target.id]] = e.target.value;
});
document.addEventListener('change', e => {
  if (e.target.id === 'rp_region') RPT.region = e.target.value;
  if (e.target.id === 'rp_area') RPT.area = e.target.value;
});
async function tkClick(t) {
  const k = t.dataset.tk;
  try {
    if (k === 'refresh') { await loadTickets(true); return; }
    if (k === 'submit') {
      const r = await api('/api/tickets', {op: 'create', title: RPT.title, what: RPT.what, steps: RPT.steps, area: RPT.area, region: RPT.region, logs: RPT.logs});
      if (r.error) return toast(r.error);
      toast('Ticket #' + r.number + ' opened'); RPT.title = RPT.what = RPT.steps = ''; sigLast = ''; render(true); loadTickets(true); return;
    }
    if (k === 'confirm' || k === 'reopen') {
      const note = k === 'reopen' ? (prompt('What is still wrong? (optional)') || '') : '';
      const r = await api('/api/tickets', {op: 'action', action: k, number: +t.dataset.n, note});
      if (r.error) return toast(r.error); toast(r.message); RPT.list = r.tickets; const el = $('#tklist'); if (el) el.innerHTML = ticketListHtml(); return;
    }
    if (k === 'savetoken') {
      const v = $('#gh_token').value.trim(); if (!v) { toast('Paste a token first'); return; }
      const r = await api('/api/tickets', {op: 'save_token', token: v}); $('#gh_token').value = '';
      toast(r.message || r.error); RPT.status = null; GHS.st = null; GHS.test = null; render(true); return; }
    if (k === 'saverepo') { const r = await api('/api/tickets', {op: 'set_repo', repo: $('#gh_repo').value}); toast(r.error || 'Repository saved'); GHS.st = null; render(true); return; }
    if (k === 'testgh') { GHS.test = {ok: true, text: 'testing...'}; render(true); const r = await api('/api/tickets', {op: 'test'}); GHS.test = {ok: !r.error, text: r.message || r.error || ''}; toast(r.message || r.error); render(true); return; }
  } catch (e) { toast('Failed: ' + e.message); }
}

const PAGE_FN = {home: pHome, games: pGames, cheats: pCheats, audio: () => pPlanned('audio'), enhance: () => pPlanned('enhance'), mods: () => pPlanned('mods'), visuals: pVisuals, access: pAccess, shots: pShots, logs: pLogs, tools: pTools, health: pHealth, changes: pChanges, about: pAbout, settings: pSettings, report: pReport};
const TITLES = {home: 'Play', games: 'Games', cheats: 'Cheats', audio: 'Audio', enhance: 'Enhancements', mods: 'Mods', visuals: 'Visuals', access: 'Accessibility', shots: 'Screenshots', logs: 'Logs', tools: 'Tools', health: 'Health', changes: 'Changelog', settings: 'Settings', about: 'About & licenses', report: 'Report an issue'};

/* ---------- render ---------- */
let renderPending = false;
function liveValues() {
  const lr = S.last_run, g = S.game;
  return {exeAge: g.exe_mtime ? age(S.now - g.exe_mtime) + ' ago' : 'not built', lastRun: lr ? age(S.now - lr.mtime) + ' ago' : 'none'};
}
function updateLive() {                                  // text that changes every second is patched in place; nothing is rebuilt, nothing jumps
  const v = liveValues();
  document.querySelectorAll('[data-live]').forEach(el => { const t = v[el.dataset.live]; if (t !== undefined && el.textContent !== t) el.textContent = t; });
}
function userBusy() {                                    // an open dropdown or a field being typed in must not be rebuilt
  const a = document.activeElement;
  return !!(a && a !== document.body && /^(SELECT|INPUT|TEXTAREA)$/.test(a.tagName) && $('#content').contains(a));
}
function render(force) {
  renderChrome(); renderNav(); updateLive();
  const sig = page + JSON.stringify([S.prefs, S.builds ? S.builds.map(b => [b.id, b.built, b.state]) : 0, S.icon_sets.map(s => s.ready), S.texpack.count, S.cheats ? S.cheats.msg : 0, page === 'games' ? S.library : 0, S.build.state, S.cards ? [S.cards.current, (S.cards.items || []).length] : 0, S.game.running, S.last_run ? [S.last_run.exact, S.last_run.icon_set, S.last_run.errors, S.last_run.texpack_on] : 0, S.scenarios.length]);
  if (!force && sig === sigLast) return;
  if (!force && userBusy()) { renderPending = true; return; }
  renderPending = false; sigLast = sig;
  $('#pagetitle').textContent = TITLES[page];
  const c = $('#content'), keep = c.scrollTop, same = (page === lastPage);
  c.innerHTML = PAGE_FN[page]();
  c.scrollTop = same ? keep : 0; lastPage = page;
  afterRender();
}
let lastPage = '';
document.addEventListener('focusout', () => { setTimeout(() => { if (renderPending && !userBusy()) render(false); }, 250); });
async function afterRender() {
  try {
    if (page === 'home') { const ch = await api('/api/changelog'); const el = $('#homechanges'); if (el) el.innerHTML = ch.slice(0, 6).map(clItem).join(''); }
    if (page === 'shots') loadShots();
    if (page === 'logs') loadLog();
    if (page === 'health') loadHealth();
    if (page === 'report') loadTickets(false);
    if (page === 'changes') loadChanges();
  } catch (e) { /* page changed meanwhile */ }
}
const clItem = e => `<div class="cl"><div class="h"><span class="id">${esc(e.id)}</span><span class="tt">${esc(e.title || e.area || '')}</span>${e.date ? `<span class="chip">${esc(e.date)}</span>` : ''}${e.ver ? `<span class="chip">${esc(e.ver)}</span>` : ''}</div>${e.bullets && e.bullets.length ? `<ul>${e.bullets.map(b => `<li>${esc(b)}</li>`).join('')}</ul>` : ''}</div>`;
async function loadShots() {
  const list = await api('/api/shots'); const g = $('#shotgrid'); if (!g) return;
  $('#shotcount').textContent = list.length + ' screenshots';
  g.innerHTML = list.length ? list.map(s => `<div class="shot" data-shot="${esc(s.path)}" data-name="${esc(s.name)}"><img loading="lazy" src="${fileUrl(s.path)}" alt=""><div class="c"><span>${esc(s.name)}</span><span>${age(Date.now() / 1000 - s.mtime)}</span></div></div>`).join('') : '<div class="empty">No screenshots yet. Press Take screenshot while the game runs.</div>';
}
function colorLine(l) {
  const lo = l.toLowerCase();
  let c = '';
  if (/error|fatal|exception|assert|crash|failed/.test(lo)) c = 'e'; else if (l.includes('[texpack]')) c = l.includes('EXACT') ? 't' : 'd'; else if (l.includes('[iconhook]')) c = 'i'; else if (/warn/.test(lo)) c = 'w';
  return `<div class="ll ${c}">${esc(l)}</div>`;
}
async function loadLog() {
  if (logState.busy) return; logState.busy = true;
  try {
    const f = S.prefs.logfilter || ''; const r = await api(`/api/log?src=${logState.src}&ver=${encodeURIComponent(logState.ver || 'pal')}&f=${encodeURIComponent(f)}`);
    const box = $('#logbox'); if (!box) return;
    const stick = logState.follow || box.scrollTop + box.clientHeight >= box.scrollHeight - 30;
    box.innerHTML = r.lines.length ? r.lines.map(colorLine).join('') : '<div class="empty">no matching lines</div>';
    if (stick) box.scrollTop = box.scrollHeight;
    $('#logmeta').textContent = `${r.matched.toLocaleString()} matching of ${r.total.toLocaleString()} lines in the last 3 MB - showing the newest ${r.lines.length}`;
  } finally { logState.busy = false; }
}
async function loadHealth() {
  healthCache = await api('/api/health'); renderNav();
  const l = $('#hlist'); if (!l) return;
  const n = k => healthCache.filter(h => h.ok === k).length;
  $('#hsum').textContent = `${n(true)} passing, ${n(null)} to look at, ${n(false)} failing`;
  l.innerHTML = healthCache.map(h => `<div class="hl"><div class="hi ${h.ok === true ? 'ok' : h.ok === false ? 'bad' : 'warn'}">${h.ok === true ? '&#10003;' : h.ok === false ? '!' : '?'}</div><div><div class="t">${esc(h.title)}</div>${h.detail ? `<div class="d">${esc(h.detail)}</div>` : ''}${h.fix ? `<div class="f">${esc(h.fix)}</div>` : ''}</div></div>`).join('');
}
let clAll = [];
async function loadChanges() {
  clAll = await api('/api/changelog'); drawChanges('');
  const q = $('#clq'); if (q) q.oninput = () => drawChanges(q.value);
}
function drawChanges(q) {
  const el = $('#cllist'); if (!el) return; q = q.toLowerCase();
  const rows = clAll.filter(e => !q || (e.id + ' ' + e.title + ' ' + (e.bullets || []).join(' ') + (e.area || '')).toLowerCase().includes(q));
  el.innerHTML = rows.length ? rows.map(clItem).join('') : '<div class="empty">nothing matches</div>';
}

/* ---------- events ---------- */
document.addEventListener('click', async ev => {
  const t = ev.target.closest('[data-pref],[data-page],[data-act],[data-go],[data-sw],[data-mode],[data-icon],[data-shot],[data-src],[data-lver],[data-chip],[data-accent],[data-theme-set],[data-cheat],[data-gset],[data-chktog],[data-chkitem],[data-chkall],[data-tk],[data-gbrowse],[data-glink],[data-gunlink],[data-gprim],[data-gscan],#lightbox');
  if (!t) return;
  if (t.id === 'lightbox') { t.hidden = true; return; }
  if (t.dataset.gbrowse || t.dataset.glink || t.dataset.gunlink || t.dataset.gprim || t.dataset.gscan) {
    const slot = t.dataset.gbrowse || t.dataset.glink || t.dataset.gunlink || t.dataset.gprim;
    const inp = slot && document.querySelector(`[data-gpath="${slot}"]`);
    try {
      let body;
      if (t.dataset.gbrowse) { toast('Opening the picker - it may appear behind this window, check the taskbar'); const r = await api('/api/games', {op: 'browse', slot, mode: t.dataset.gmode || 'folder'}); if (!r.path) return toast(r.error ? 'Picker problem: ' + r.error + ' - paste the path instead' : 'Nothing picked'); inp.value = r.path; body = {op: 'link', slot, path: r.path}; }
      else if (t.dataset.glink) body = {op: 'link', slot, path: inp.value};
      else if (t.dataset.gunlink) body = {op: 'unlink', slot};
      else if (t.dataset.gprim) body = {op: 'primary', slot};
      else body = {op: 'rescan'};
      const r = await api('/api/games', body); toast(r.message); if (r.library) S.library = r.library; if (r.env) S.env = r.env; await refresh(); render(true);
    } catch (e) { toast('Failed: ' + e.message); }
    return;
  }
  if (t.dataset.chktog) { S.chkopen = Object.assign({}, S.chkopen || {}); S.chkopen[t.dataset.chktog] = !S.chkopen[t.dataset.chktog]; return render(true); }
  if (t.dataset.chkall || t.dataset.chkitem) {
    const [id, x] = (t.dataset.chkall || t.dataset.chkitem).split(':'), def = S.cheats.defs.find(d => d.id === id);
    let cur = Array.isArray(cheatStore()[id]) ? cheatStore()[id].slice() : [];
    if (t.dataset.chkall) cur = x === '1' ? def.items.map(i => i[0]) : [];
    else { const n = +x; cur = cur.includes(n) ? cur.filter(q => q !== n) : cur.concat(n); }
    return cheatSet(id, cur);
  }
  if (t.dataset.gset) { try { const r = await api('/api/action', {name: 'gset', key: t.dataset.gset, value: +t.dataset.val}); S.gset = Object.assign({}, S.gset, {[t.dataset.gset]: +t.dataset.val}); toast(r.message); render(true); } catch (e) { toast('Failed: ' + e.message); } return; }
  if (t.dataset.cheat) return cheatSet(t.dataset.cheat, t.dataset.val);
  if (t.dataset.page) return go(t.dataset.page);
  if (t.dataset.go) { if (t.dataset.filter) S.prefs.logfilter = t.dataset.filter; return go(t.dataset.go); }
  if (t.dataset.mode) return setPref('mode', t.dataset.mode);
  if (t.dataset.icon !== undefined) { const s = S.icon_sets.find(x => x.id === t.dataset.icon); if (s && !s.ready) return toast(s.name + ' has no data yet - build it with the icon tools'); return setPref('icons', t.dataset.icon); }
  if (t.dataset.sw) {
    const k = t.dataset.sw;
    if (k === 'follow') { logState.follow = !logState.follow; return render(true); }
    if (k.startsWith('cheat:')) { const ck = k.slice(6); return cheatSet(ck, !cheatStore()[ck]); }
    if (k === 'rp_logs') { RPT.logs = !RPT.logs; sigLast = ''; return render(true); }
    return setPref(k, !S.prefs[k]);
  }
  if (t.dataset.tk) return tkClick(t);
  if (t.dataset.shot) { $('#lbimg').src = fileUrl(t.dataset.shot); $('#lbcap').textContent = t.dataset.name; $('#lightbox').hidden = false; return; }
  if (t.dataset.lver) { logState.ver = t.dataset.lver; return render(true); }
  if (t.dataset.src) { logState.src = t.dataset.src; return render(true); }
  if (t.dataset.chip !== undefined) { await setPref('logfilter', t.dataset.chip); return; }
  if (t.dataset.pref) { await setPref(t.dataset.pref, t.dataset.val); sigLast = ''; render(true); return; }
  if (t.dataset.accent) return setPref('accent', t.dataset.accent);
  if (t.dataset.themeSet) return setPref('theme', t.dataset.themeSet);
  if (t.dataset.act) {
    const a = t.dataset.act;
    if (a === 'rehealth') return loadHealth();
    if (a === 'resetprefs') { await api('/api/prefs', {mode: 'normal', icons: '', texpack: false, probes: true, skipmovies: false, accent: '#e11d48', theme: 'dark', autoquit: true, logfilter: '', arcade: false, psp_primary: ''}); await refresh(); applyTheme(); return render(true); }
    if (a === 'exitlauncher') { try { await api('/api/exit', {}); } catch (e) {} window.close(); return; }
    if (a === 'newcard') { const nm = window.prompt('Name for the new memory card (leave empty for a dated name):', ''); if (nm === null) return; return act('newcard', nm.trim()); }
    return act(a, t.dataset.arg);
  }
});
document.addEventListener('keydown', e => { if (e.key === 'Escape') $('#lightbox').hidden = true; });
document.addEventListener('input', e => {
  if (e.target.id === 'logfilter') { clearTimeout(window._lf); window._lf = setTimeout(async () => { S.prefs.logfilter = e.target.value; await api('/api/prefs', {logfilter: e.target.value}); loadLog(); }, 300); }
});
$('#topquit').onclick = () => act('quit');
function go(p) { page = p; try { history.replaceState(null, '', '#' + p); } catch (e) {} sigLast = ''; render(true); }

let failN = 0, refreshBusy = false;
async function refresh() {
  if (refreshBusy) return; refreshBusy = true;
  try { S = await api('/api/state'); failN = 0; render(false); }
  catch (e) { if (++failN >= 4) $('#statustext').textContent = 'launcher service not reachable'; }
  finally { refreshBusy = false; }
}
(async function init() {
  S = await api('/api/state'); applyTheme();
  const h = location.hash.slice(1); page = PAGE_FN[h] ? h : (PAGE_FN[S.prefs.page] ? S.prefs.page : 'home');
  render(true); loadHealth().catch(() => {});
  setInterval(refresh, 1500);
  setInterval(() => { if (page === 'logs') loadLog(); }, 1500);
  setInterval(() => api('/api/ping').catch(() => {}), 4000); api('/api/ping').catch(() => {});
})();

document.addEventListener('change', async e => {
  const t = e.target;
  if (t && t.id === 'card_file') { await setPref('card_file', t.value.trim()); return; }
  if (t && t.id === 'cardsel') { await act('cardsel', t.value); await refresh(); render(true); return; }
  if (t && t.id === 'buildsel') { await setPref('build', t.value); await refresh(); render(true); }
});
