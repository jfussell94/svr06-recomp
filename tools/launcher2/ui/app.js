'use strict';
/* SvR06 Launcher 2 - UI. No framework: pages are built with h(), controls register a sync function so live values from the game's F1 menu
   update in place without rebuilding the page (nothing flickers, nothing steals focus while you drag a slider). */

const $ = (s, r = document) => r.querySelector(s);
const ICON = {
  play: '<path d="M6 4l11 6-11 6z"/>', graphics: '<rect x="2.5" y="3.5" width="15" height="10" rx="1.5"/><path d="M7 17h6M10 13.5V17"/>',
  audio: '<path d="M3 8v4h3l4 3.5v-11L6 8z"/><path d="M13 7.5a3.5 3.5 0 010 5M15.2 5.2a7 7 0 010 9.6"/>',
  controls: '<path d="M5.5 6h9a3.5 3.5 0 013.4 4.3l-.9 3.6a2 2 0 01-3.3.9L12 13H8l-1.7 1.8a2 2 0 01-3.3-.9l-.9-3.6A3.5 3.5 0 015.5 6z"/><path d="M6.5 8.5v3M5 10h3M13 9.3h.01M14.8 10.8h.01"/>',
  gameplay: '<path d="M4 6h7M15 6h1M4 10h2M10 10h6M4 14h9M17 14h-1"/><circle cx="13" cy="6" r="1.7"/><circle cx="8" cy="10" r="1.7"/><circle cx="15" cy="14" r="1.7"/>',
  cheats: '<path d="M10 2.5l1.7 4.3 4.3 1.7-4.3 1.7L10 14.5l-1.7-4.3L4 8.5l4.3-1.7zM15.5 13l.7 1.8 1.8.7-1.8.7-.7 1.8-.7-1.8-1.8-.7 1.8-.7z"/>',
  mods: '<path d="M10 2.5l7 3.8v7.4l-7 3.8-7-3.8V6.3z"/><path d="M3.2 6.4L10 10l6.8-3.6M10 10v7.5"/>',
  saves: '<path d="M4 3.5h9l3 3V16a.5.5 0 01-.5.5h-11A.5.5 0 014 16z"/><path d="M7 3.5v4h5v-4M7 16.5v-5h6v5"/>',
  library: '<path d="M4.5 3.5v13M8.5 3.5v13M12 4.2l3.6 12.3"/><path d="M3 3.5h7M3 16.5h7"/>',
  tools: '<path d="M12.5 3.5a4 4 0 00-3.7 5.4L3.5 14.2a1.6 1.6 0 002.3 2.3l5.3-5.3a4 4 0 005.4-3.7l-2.4 2.4-2.1-.4-.4-2.1z"/>',
  launcher: '<circle cx="10" cy="10" r="2.6"/><path d="M10 2.5v2.2M10 15.3v2.2M2.5 10h2.2M15.3 10h2.2M4.7 4.7l1.5 1.5M13.8 13.8l1.5 1.5M4.7 15.3l1.5-1.5M13.8 6.2l1.5-1.5"/>',
  reset: '<path d="M4 10a6 6 0 106-6H7M7 1.5L4.5 4 7 6.5" transform="translate(0 1)"/>', check: '<path d="M4 10.5l4 4 8-9"/>',
};
const ic = n => `<svg viewBox="0 0 20 20" aria-hidden="true">${ICON[n] || ''}</svg>`;

function h(tag, attrs, ...kids) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (v == null || v === false) continue;
    if (k === 'class') e.className = v;
    else if (k === 'html') e.innerHTML = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'dataset') Object.assign(e.dataset, v);
    else e.setAttribute(k, v === true ? '' : v);
  }
  for (const c of kids.flat()) if (c != null && c !== false) e.append(c.nodeType ? c : document.createTextNode(c));
  return e;
}

// ---------------------------------------------------------------- state + api
const S = { schema: null, snap: null, page: 'play', query: '', pending: {}, more: new Set(), syncs: {}, live: {}, dragging: null, sig: '', health: null, changelog: null,
            logsrc: 'run_err', logfilter: '', ui: {} };

async function api(path, body) {
  const opt = { headers: { 'X-Token': window.TOKEN } };
  if (body !== undefined) { opt.method = 'POST'; opt.body = JSON.stringify(body); opt.headers['Content-Type'] = 'application/json'; }
  const r = await fetch(path, opt);
  if (!r.ok) throw new Error((await r.json().catch(() => ({}))).error || r.statusText);
  return r.json();
}
function toast(msg, kind) {
  if (!msg) return;
  const t = h('div', { class: 'toast ' + (kind || '') }, msg);
  $('#toasts').append(t);
  setTimeout(() => t.remove(), kind === 'bad' ? 7000 : 4200);
}
const heavy = () => (S.snap && S.snap.heavy) || {};
const light = () => (S.snap && S.snap.light) || {};
const sel = () => heavy().sel || 'pal';
const running = () => !!light().running;
function val(key) {
  if (key in S.pending) return S.pending[key];
  const v = (heavy().values || {})[key];
  return v === undefined ? S.schema.byKey[key].default : v;
}

// ---------------------------------------------------------------- applying UI prefs
function applyUi(u) {
  S.ui = u; const r = document.documentElement;
  let th = u.theme;
  if (th === 'system') th = matchMedia('(prefers-color-scheme: light)').matches ? 'canvas' : 'graphite';
  r.dataset.theme = th; r.dataset.accent = u.accent; r.dataset.density = u.density; r.dataset.motion = u.motion === 'reduced' ? 'reduced' : 'full';
  r.style.setProperty('--tscale', (u.textScale || 100) / 100);
}
async function setUi(patch) { applyUi(Object.assign({}, S.ui, patch)); try { await api('/api/ui', patch); } catch (e) { toast(e.message, 'bad'); } }

// ---------------------------------------------------------------- setting writes
let sendTimer = null; let sendBuf = {};
function setValue(key, v, immediate) {
  S.pending[key] = v; sendBuf[key] = v;
  const s = S.schema.byKey[key]; if (s) refreshReset(key);
  clearTimeout(sendTimer);
  sendTimer = setTimeout(flush, immediate ? 0 : 250);
}
async function flush() {
  const ch = sendBuf; sendBuf = {}; if (!Object.keys(ch).length) return;
  try {
    const r = await api('/api/set', { changes: ch });
    toast(r.message, r.restart ? 'warn' : '');
    const sn = await api('/api/snapshot'); S.snap = sn;
    for (const k of Object.keys(ch)) delete S.pending[k];
    syncAll();
  } catch (e) { toast('Could not save: ' + e.message, 'bad'); for (const k of Object.keys(ch)) delete S.pending[k]; syncAll(); }
}

// ---------------------------------------------------------------- setting controls
const TAGS = { restart: ['Restart needed', 'restart'], dev: ['In development', 'dev'], 'pal-only': ['PAL only', 'dev'], 'needs-psp': ['Needs PSP game', 'dev'], 'usa-pal': ['USA / PAL', 'dev'] };

function controlFor(s) {
  const k = s.key; const wrap = h('div', { class: 'ctl' });
  const reg = fn => { (S.syncs[k] = S.syncs[k] || []).push(fn); };
  if (s.kind === 'toggle') {
    const inp = h('input', { type: 'checkbox', role: 'switch', 'aria-label': s.label });
    inp.addEventListener('change', () => setValue(k, inp.checked ? 1 : 0, true));
    wrap.append(h('label', { class: 'sw' }, inp, h('i')));
    reg(() => { inp.checked = !!val(k); });
  } else if (s.kind === 'segmented') {
    const bs = s.options.map(o => h('button', { type: 'button', title: o.hint || '', 'aria-pressed': 'false', onclick: () => setValue(k, o.v, true) }, o.label));
    wrap.append(h('div', { class: 'seg', role: 'group', 'aria-label': s.label }, bs));
    reg(() => bs.forEach((b, i) => b.setAttribute('aria-pressed', String(val(k) === s.options[i].v))));
  } else if (s.kind === 'select') {
    const sl = h('select', { class: 'sel', 'aria-label': s.label }, s.options.map(o => h('option', { value: o.v }, o.label + (o.hint ? '  -  ' + o.hint : ''))));
    sl.addEventListener('change', () => setValue(k, +sl.value, true));
    wrap.append(sl); reg(() => { sl.value = String(val(k)); });
  } else if (s.kind === 'slider' || s.kind === 'fader') {
    const out = h('output'); const inp = h('input', { type: 'range', min: s.min, max: s.max, step: s.step || 1, 'aria-label': s.label });
    const paint = () => { const v = +inp.value; inp.style.setProperty('--p', ((v - s.min) / (s.max - s.min) * 100) + '%'); out.textContent = v + (s.unit || ''); };
    inp.addEventListener('input', () => { S.dragging = k; paint(); setValue(k, +inp.value); });
    inp.addEventListener('change', () => { S.dragging = null; });
    wrap.append(h('div', { class: 'rng' }, inp, out));
    reg(() => { if (S.dragging === k) return; inp.value = val(k); paint(); });
  } else if (s.kind === 'cards') {
    const cs = s.options.map(o => h('button', { type: 'button', class: 'card', 'aria-pressed': 'false', onclick: () => setValue(k, o.v, true) },
      h('b', {}, o.label), h('span', {}, o.hint || ''), h('i', { class: 'chk', html: ic('check') })));
    const g = h('div', { class: 'cards' }, cs); wrap.append(g); wrap.classList.add('full');
    reg(() => cs.forEach((b, i) => b.setAttribute('aria-pressed', String(val(k) === s.options[i].v))));
  } else if (s.kind === 'icons') {
    const g = h('div', { class: 'cards' }); wrap.append(g);
    let built = '';
    reg(() => {
      const sets = heavy().icon_sets || []; const sig = JSON.stringify(sets.map(x => [x.id, x.ready, x.name]));
      const lock = sel() !== 'pal';
      if (sig !== built) {
        built = sig; g.replaceChildren(...sets.map((x, i) => h('button', { type: 'button', class: 'card' + (x.ready ? '' : ' off'), 'aria-pressed': 'false', dataset: { i },
          onclick: () => x.ready && !lock ? setValue(k, i, true) : toast(lock ? 'Button icons only work on the PAL version for now.' : x.name + ' is not installed yet.', 'warn') },
          h('b', {}, x.name), h('span', {}, x.ready ? x.sub : 'Not installed'), h('i', { class: 'chk', html: ic('check') }))));
      }
      [...g.children].forEach((b, i) => b.setAttribute('aria-pressed', String(val(k) === i)));
    });
  } else if (s.kind === 'gpu') {
    const sl = h('select', { class: 'sel', 'aria-label': s.label });
    sl.addEventListener('change', () => setValue(k, sl.value, true));
    const note = h('span', { class: 'tag' }, '');
    wrap.append(sl);
    let built = '';
    reg(() => {
      const gp = (heavy().gpus || []).map(x => x.name); const cur = val(k) || '';
      const sig = gp.join('|') + '#' + cur; if (sig !== built) {
        built = sig; const opts = [h('option', { value: '' }, 'Automatic')]; const all = gp.slice(); if (cur && !all.includes(cur)) all.push(cur);
        all.forEach(n => opts.push(h('option', { value: n }, n))); sl.replaceChildren(...opts);
      }
      sl.value = cur;
    });
  }
  return wrap;
}

function settingRow(s, opts) {
  opts = opts || {};
  const k = s.key;
  const tags = [];
  if (s.applies === 'restart') tags.push(h('span', { class: 'tag restart' }, TAGS.restart[0]));
  for (const t of s.tags) if (TAGS[t] && t !== 'usa-pal') tags.push(h('span', { class: 'tag dev' }, TAGS[t][0]));
  const recT = s.tags.find(t => t.startsWith('recommended:'));
  if (recT && s.options.length) {
    const rv = +recT.split(':')[1]; const o = s.options.find(o => o.v === rv);
    if (o) tags.push(h('span', { class: 'tag acc', title: 'Recommended setting' }, 'Recommended: ' + o.label));
  } else if (recT) tags.push(h('span', { class: 'tag acc' }, 'Recommended'));
  const resetBtn = h('button', { type: 'button', class: 'reset', title: 'Reset to default', 'aria-label': 'Reset ' + s.label + ' to default', hidden: true, html: ic('reset'),
    onclick: () => setValue(k, s.default, true) });
  (S.resets = S.resets || {})[k] = resetBtn;
  const ctl = controlFor(s);
  const full = s.kind === 'cards' || s.kind === 'icons';
  const hasMore = !!s.detail;
  const more = h('div', { class: 'more' }, s.detail);
  const moreBtn = hasMore ? h('button', { type: 'button', class: 'link', 'aria-expanded': String(S.more.has(k)), onclick: e => {
    if (S.more.has(k)) { S.more.delete(k); more.hidden = true; } else { S.more.add(k); more.hidden = false; }
    e.target.setAttribute('aria-expanded', String(S.more.has(k))); e.target.textContent = S.more.has(k) ? 'Less' : 'More detail';
  } }, S.more.has(k) ? 'Less' : 'More detail') : null;
  more.hidden = !S.more.has(k);
  const kids = [h('div', {},
    h('div', { class: 'lab' }, s.label, tags, resetBtn),
    h('p', { class: 'desc' }, s.desc, hasMore ? ' ' : '', moreBtn), hasMore ? more : null)];
  const row = h('div', { class: 'row' + (full ? ' block' : ''), dataset: { key: k } }, kids, full ? null : null);
  if (full) { row.append(ctl); } else { row.append(ctl); }
  if (s.kind === 'gpu') {
    const used = h('p', { class: 'desc', style: 'margin-top:6px' }); row.firstChild.append(used);
    (S.syncs[k] = S.syncs[k] || []).push(() => { const u = heavy().gpu_used; used.textContent = u ? 'Last used: ' + u : ''; });
  }
  return row;
}
function refreshReset(key) {
  const b = (S.resets || {})[key], s = S.schema.byKey[key]; if (!b || !s) return;
  const v = val(key); b.hidden = v === s.default || (s.kind === 'gpu' && !v);
}
function syncAll() {
  for (const [k, fns] of Object.entries(S.syncs)) { for (const f of fns) { try { f(); } catch (e) { /* control removed */ } } refreshReset(k); }
  updateLive();
}

function section(title, lead, ...rows) { return h('section', { class: 'sec' }, h('h2', {}, title), lead ? h('p', { class: 'lead' }, lead) : null, h('div', { class: 'rows' }, rows.flat())); }

// ---------------------------------------------------------------- pages
const PAGE_BLURB = {};
function settingsPage(pid, extras) {
  const list = S.schema.settings.filter(s => s.page === pid);
  const secs = []; const order = [];
  for (const s of list) { if (!order.includes(s.section)) order.push(s.section); }
  const wrap = h('div', { class: 'wrap' });
  if (extras && extras.top) wrap.append(extras.top);
  for (const name of order) {
    const items = list.filter(s => s.section === name);
    wrap.append(section(name, (extras && extras.lead && extras.lead[name]) || '', items.map(s => settingRow(s))));
    if (extras && extras.after && extras.after[name]) wrap.append(extras.after[name]);
  }
  wrap.append(h('div', { style: 'margin-top:26px' }, h('button', { type: 'button', class: 'btn sm', onclick: async () => {
    if (!confirm('Reset every setting on this page to its default?')) return;
    const r = await api('/api/reset', { page: pid }); toast(r.message); S.snap = await api('/api/snapshot'); syncAll();
  } }, 'Reset this page to defaults')));
  return wrap;
}

function presetMatch() {
  for (const p of S.schema.presets) {
    if (Object.entries(p.values).every(([k, v]) => val(k) === v)) return p.id;
  }
  return '';
}
function presetTiles() {
  const tiles = S.schema.presets.map(p => h('button', { type: 'button', class: 'card', 'aria-pressed': 'false', dataset: { p: p.id }, onclick: async () => {
    const r = await api('/api/preset', { id: p.id }); toast(r.message, r.restart ? 'warn' : ''); S.snap = await api('/api/snapshot'); syncAll();
  } }, h('b', {}, p.name), h('span', {}, p.sub), h('span', {}, p.desc), h('i', { class: 'chk', html: ic('check') })));
  const g = h('div', { class: 'cards' }, tiles);
  const custom = h('p', { class: 'desc', style: 'margin-top:8px' });
  (S.syncs.__preset = S.syncs.__preset || []).push(() => {
    const m = presetMatch(); tiles.forEach(t => t.setAttribute('aria-pressed', String(t.dataset.p === m)));
    custom.textContent = m ? '' : 'You have changed individual settings, so no preset is highlighted.';
  });
  return h('div', {}, g, custom);
}

function pageGraphics() {
  const top = h('section', { class: 'sec', style: 'margin-top:14px' }, h('h2', {}, 'Presets'),
    h('p', { class: 'lead' }, 'One click sets resolution, edges and picture together. You can still change any single option below afterwards.'), presetTiles());
  return settingsPage('graphics', { top });
}
function pageAudio() { return settingsPage('audio'); }
function pageControls() {
  const note = h('section', { class: 'sec' }, h('h2', {}, 'In-game keys'), h('div', { class: 'cols', style: 'margin-top:10px' },
    S.schema.hotkeys.map(([k, d]) => h('div', { class: 'panel' }, h('span', { class: 'kbd' }, k), h('p', { class: 'desc', style: 'margin-top:8px;color:var(--dim)' }, d)))));
  const w = settingsPage('controls'); w.append(note); return w;
}
function pageGameplay() { return settingsPage('gameplay'); }

// ---- play
function ago(sec) { if (sec < 90) return 'just now'; if (sec < 5400) return Math.round(sec / 60) + ' min ago'; if (sec < 129600) return Math.round(sec / 3600) + ' h ago'; return Math.round(sec / 86400) + ' days ago'; }
const STAGE_TAG = { planned: '', bootable: '', ingame: 'good' };

function pagePlay() {
  const w = h('div', { class: 'wrap' });
  const b = (heavy().builds || []).find(x => x.id === sel()) || {};
  const live = h('div', { class: 'live', id: 'liveBox' }, h('div', {}, h('b', { id: 'lvFps' }, '-'), h('span', {}, 'frames per second')), h('div', {}, h('b', { id: 'lvSpd' }, '-'), h('span', {}, 'game speed')),
    h('div', {}, h('b', { id: 'lvVs' }, '-'), h('span', {}, 'frames shown')), h('div', {}, h('b', { id: 'lvUp' }, '-'), h('span', {}, 'session time')));
  const playBtn = h('button', { type: 'button', class: 'play', id: 'playBtn', onclick: () => act('play', sel()) }, h('span', { html: '<svg viewBox="0 0 20 20"><path d="M5 3l12 7-12 7z"/></svg>' }), 'Play');
  const hero = h('div', { class: 'hero' },
    h('div', {}, h('h2', {}, b.name || 'Game'), h('div', { class: 'meta', id: 'heroMeta' }), b.note ? h('p', { class: 'note' }, b.note) : null,
      h('div', { class: 'acts', id: 'heroActs' }, playBtn,
        h('button', { type: 'button', class: 'btn', id: 'rebBtn', title: 'Compile the latest changes, then start the game', onclick: () => act('rebuild', sel()) }, 'Rebuild and play'),
        h('button', { type: 'button', class: 'btn danger', id: 'quitBtn', hidden: true, onclick: () => act('quit', '') }, 'Quit game'),
        h('button', { type: 'button', class: 'btn', id: 'shotBtn', hidden: true, onclick: () => act('shot', '') }, 'Screenshot')),
      h('div', { id: 'buildBar' })), live);
  w.append(hero);
  w.append(h('div', { id: 'playWarn' }));
  // versions
  const vers = (heavy().builds || []).filter(x => !x.planned).map(x => h('button', { type: 'button', class: 'ver', 'aria-pressed': String(x.id === sel()), onclick: async () => {
    await api('/api/version', { id: x.id }); S.snap = await api('/api/snapshot'); buildPage(); } },
    h('b', {}, x.name), h('div', { class: 'tags' }, h('span', { class: 'tag ' + (x.built ? 'good' : 'bad') }, x.built ? 'Built' : 'Not built'), h('span', { class: 'tag' }, x.stage_label || ''),
      !x.has_elf ? h('span', { class: 'tag bad' }, 'Game files missing') : null)));
  const planned = (heavy().builds || []).filter(x => x.planned);
  w.append(h('section', { class: 'sec' }, h('h2', {}, 'Version'), h('p', { class: 'lead' }, 'Every version keeps its own settings, memory cards and cheats.'), h('div', { class: 'vers' }, vers),
    planned.length ? h('p', { class: 'desc', style: 'margin-top:10px;color:var(--faint)' }, 'Planned: ' + planned.map(p => p.name.replace(' (planned)', '')).join(', ')) : null));
  // look
  w.append(h('section', { class: 'sec' }, h('h2', {}, 'Picture'), h('p', { class: 'lead' }, 'A quick start. Open Graphics for every option.'), presetTiles()));
  const quick = ['graphics.fullscreen', 'graphics.resolution', 'game.skipMovies', 'game.texpack'].map(k => settingRow(S.schema.byKey[k]));
  w.append(section('Quick switches', '', quick));
  // health
  const hb = h('div', { id: 'healthBox' });
  w.append(h('section', { class: 'sec' }, h('h2', {}, 'Needs attention'), hb));
  loadHealth().then(() => renderHealthSummary(hb));
  // news
  const nb = h('div', { id: 'newsBox' });
  w.append(h('section', { class: 'sec' }, h('h2', {}, 'Recent changes'), nb));
  loadChangelog().then(() => {
    const es = (S.changelog || []).slice(0, 4);
    nb.replaceChildren(...(es.length ? [h('div', { class: 'list' }, es.map(e => h('div', { class: 'li' }, h('i', { class: 'st ok' }), h('div', {}, h('b', {}, e.title || e.id), (e.bullets || [])[0] ? h('p', {}, e.bullets[0]) : null), h('span', { class: 'tag' }, e.id))))] : [h('p', { class: 'desc' }, 'No change log found.')]));
  });
  return w;
}
async function loadHealth(force) { if (S.health && !force) return S.health; try { S.health = await api('/api/health'); } catch (e) { S.health = []; } return S.health; }
async function loadChangelog() { if (S.changelog) return S.changelog; try { S.changelog = await api('/api/changelog'); } catch (e) { S.changelog = []; } return S.changelog; }
function renderHealthSummary(box) {
  const bad = (S.health || []).filter(x => x.ok === false);
  if (!bad.length) { box.replaceChildren(h('p', { class: 'desc' }, 'Everything the launcher checks looks fine.')); return; }
  box.replaceChildren(h('div', { class: 'list' }, bad.slice(0, 4).map(x => h('div', { class: 'li' }, h('i', { class: 'st bad' }), h('div', {}, h('b', {}, x.title), h('p', {}, x.fix || x.detail || '')), null))),
    h('button', { type: 'button', class: 'link', style: 'margin-top:8px', onclick: () => go('tools') }, 'Open the full health check'));
}
async function act(name, arg, extra) {
  try { const r = await api('/api/action', Object.assign({ name, arg }, extra || {})); toast(r.message); S.snap = await api('/api/snapshot'); updateLive(); return r; }
  catch (e) { toast(e.message, 'bad'); }
}

function updateLive() {
  const L = light(), H = heavy(); const run = !!L.running;
  const set = (id, t) => { const e = document.getElementById(id); if (e && e.textContent !== t) e.textContent = t; };
  const fl = (S.snap && S.snap.light && S.snap.light.hb && S.snap.light.hb.fields) || {};
  const sp = L.speed;
  const dot = $('#railDot'), rt = $('#railTxt');
  if (dot) { dot.classList.toggle('on', run); }
  if (rt) rt.textContent = run ? 'Game running' : 'Game closed';
  const lb = $('#liveBox'); if (!lb) return;
  lb.style.display = run ? '' : 'none';
  $('#quitBtn').hidden = !run; $('#shotBtn').hidden = !run;
  const b = (H.builds || []).find(x => x.id === sel()) || {};
  const pb = $('#playBtn'); pb.disabled = run || !b.built; $('#rebBtn').disabled = run;
  const bs = L.build || {};
  const meta = $('#heroMeta'); const parts = [];
  if (run) parts.push(h('span', { class: 'tag good' }, 'Running'));
  else if (!b.built) parts.push(h('span', { class: 'tag bad' }, 'Not built yet'));
  else parts.push(h('span', { class: 'tag good' }, 'Ready'), h('span', {}, 'Built ' + ago(Date.now() / 1000 - b.exe_mtime)));
  if (b.serial) parts.push(h('span', {}, b.serial));
  if (H.version) parts.push(h('span', {}, H.version));
  const sig = parts.map(p => p.textContent).join('|'); if (meta.dataset.sig !== sig) { meta.dataset.sig = sig; meta.replaceChildren(...parts); }
  const bb = $('#buildBar');
  if (bs.state === 'running' || bs.state === 'linking') {
    const pct = bs.total ? Math.min(100, Math.round(bs.n / bs.total * 100)) : 0;
    bb.replaceChildren(h('p', { class: 'desc', style: 'margin-top:14px' }, bs.state === 'linking' ? 'Linking the game (the last step, can take a minute)...' : `Building ${bs.n} of ${bs.total}`), h('div', { class: 'bar' }, h('i', { style: 'width:' + (bs.state === 'linking' ? 100 : pct) + '%' })));
  } else if (bs.state === 'failed') bb.replaceChildren(h('div', { class: 'note-box bad' }, 'The last build failed. Open Tools, then Logs, then Build log to see why.'));
  else bb.replaceChildren();
  if (run) {
    set('lvFps', fl.fps || fl.presented || '-'); set('lvSpd', sp && sp.pct != null ? Math.round(sp.pct) + '%' : (sp && sp.speed ? Math.round(sp.speed * 100) + '%' : '-'));
    set('lvVs', fl.presented || fl.vsync || '-'); set('lvUp', fmtDur(sp && sp.secs));
  }
}
function fmtDur(s) { if (!s) return '-'; s = Math.round(s); const m = Math.floor(s / 60); return m >= 60 ? Math.floor(m / 60) + ' h ' + (m % 60) + ' min' : m + ' min ' + (s % 60) + ' s'; }

// ---- cheats
function pageCheats() {
  const C = heavy().cheats || { defs: [], values: {} };
  const w = h('div', { class: 'wrap' });
  w.append(h('div', { class: 'note-box' + (C.verified ? '' : ' warn'), style: 'margin-top:10px' },
    C.verified ? 'Cheats are applied to the running game by the launcher, so keep it open while you play. Start the game, then switch cheats on.'
      : 'Cheats are not available for this version yet: its memory map has not been verified, so the launcher will not write to the game.'));
  const groups = [];
  C.defs.forEach(c => { if (!groups.includes(c.g)) groups.push(c.g); });
  const STAT = { working: ['Works', 'good'], untested: ['Untested', ''], todo: ['Not finished', 'dev'] };
  for (const g of groups) {
    const rows = C.defs.filter(c => c.g === g).map(c => cheatRow(c, STAT));
    w.append(section(g, '', rows));
  }
  return w;
}
function cheatVal(key) { return (heavy().cheats || { values: {} }).values[key]; }
function cheatRow(c, STAT) {
  const reg = fn => { (S.syncs['cheat.' + c.id] = S.syncs['cheat.' + c.id] || []).push(fn); };
  const st = STAT[c.status] || ['', ''];
  const lab = h('div', { class: 'lab' }, c.name, st[0] ? h('span', { class: 'tag ' + st[1] }, st[0]) : null);
  const ctl = h('div', { class: 'ctl' });
  const send = (key, v) => { const cv = (heavy().cheats.values = heavy().cheats.values || {}); cv[key] = v; api('/api/cheat', { key, value: v }).catch(e => toast(e.message, 'bad')); syncAll(); };
  const players = c.per ? [['p1', 'Player 1'], ['p2', 'Player 2']] : [['', '']];
  if (c.kind === 'checks') {
    const btn = h('button', { type: 'button', class: 'btn sm' }, 'Choose...');
    btn.onclick = () => checksDialog(c, send);
    ctl.append(btn); reg(() => { const n = (cheatVal(c.id) || []).length; btn.textContent = n ? n + ' of ' + c.items.length + ' selected - change' : 'Choose...'; });
  } else for (const [who, wl] of players) {
    const key = c.id + (who ? '.' + who : '');
    if (c.kind === 'switch') {
      const inp = h('input', { type: 'checkbox', role: 'switch', 'aria-label': c.name + ' ' + wl }); inp.addEventListener('change', () => send(key, inp.checked));
      ctl.append(h('label', { style: 'display:flex;gap:8px;align-items:center' }, wl ? h('span', { style: 'color:var(--dim);font-size:.84rem' }, wl) : null, h('span', { class: 'sw' }, inp, h('i'))));
      reg(() => { inp.checked = !!cheatVal(key); });
    } else {
      const bs = c.opts.map(o => h('button', { type: 'button', 'aria-pressed': 'false', onclick: () => send(key, o) }, o === 'off' ? 'Off' : o === 'unlimited' ? 'Unlimited' : o));
      ctl.append(h('div', { style: 'display:flex;gap:8px;align-items:center;flex-wrap:wrap;justify-content:flex-end' }, wl ? h('span', { style: 'color:var(--dim);font-size:.84rem' }, wl) : null, h('div', { class: 'seg' }, bs)));
      reg(() => bs.forEach((b, i) => b.setAttribute('aria-pressed', String((cheatVal(key) || 'off') === c.opts[i]))));
    }
  }
  const row = h('div', { class: 'row', dataset: { key: 'cheat.' + c.id } }, h('div', {}, lab, c.desc ? h('p', { class: 'desc' }, c.desc) : null), ctl);
  return row;
}
function checksDialog(c, send) {
  const cur = new Set(cheatVal(c.id) || []);
  const list = h('div'); const q = h('input', { type: 'search', class: 'sel', placeholder: 'Filter this list', style: 'width:100%;margin:6px 0 10px' });
  const rows = c.items.map(([n, nm]) => { const cb = h('input', { type: 'checkbox' }); cb.checked = cur.has(n); cb.onchange = () => { cb.checked ? cur.add(n) : cur.delete(n); };
    return { el: h('label', { class: 'chk-row' }, cb, h('span', {}, nm)), cb, nm: nm.toLowerCase(), n }; });
  list.append(...rows.map(r => r.el));
  q.oninput = () => rows.forEach(r => r.el.style.display = r.nm.includes(q.value.toLowerCase()) ? '' : 'none');
  openModal(h('div', { class: 'dlg' }, h('header', {}, h('h3', {}, c.name), h('p', {}, c.desc)), h('div', { class: 'body' }, q, list),
    h('footer', {}, h('button', { class: 'btn sm', onclick: () => rows.forEach(r => { if (r.el.style.display !== 'none') { r.cb.checked = true; cur.add(r.n); } }) }, 'Select shown'),
      h('button', { class: 'btn sm', onclick: () => rows.forEach(r => { if (r.el.style.display !== 'none') { r.cb.checked = false; cur.delete(r.n); } }) }, 'Clear shown'),
      h('span', { style: 'flex:1' }), h('button', { class: 'btn', onclick: closeModal }, 'Cancel'),
      h('button', { class: 'btn pri', onclick: () => { send(c.id, [...cur].sort((a, b) => a - b)); closeModal(); } }, 'Save'))));
}
function openModal(node) { const m = $('#modal'); m.replaceChildren(node); m.hidden = false; m.onclick = e => { if (e.target === m) closeModal(); }; }
function closeModal() { const m = $('#modal'); m.hidden = true; m.replaceChildren(); }
function promptDialog(title, text, initial, ok, cb) {
  const inp = h('input', { type: 'text', class: 'sel', value: initial || '', style: 'width:100%' });
  const go2 = () => { const v = inp.value.trim(); closeModal(); cb(v); };
  inp.addEventListener('keydown', e => { if (e.key === 'Enter') go2(); });
  openModal(h('div', { class: 'dlg' }, h('header', {}, h('h3', {}, title), h('p', {}, text)), h('div', { class: 'body' }, inp), h('footer', {}, h('button', { class: 'btn', onclick: closeModal }, 'Cancel'), h('button', { class: 'btn pri', onclick: go2 }, ok))));
  setTimeout(() => inp.focus(), 30);
}

// ---- characters: import wrestlers from older SvR games (CL-0315)
function charactersSection() {
  const box = h('section', { class: 'sec' });
  const state = { d: null, busy: false };
  const draw = () => {
    const d = state.d; box.replaceChildren(h('h2', {}, 'Characters'), h('p', { class: 'lead' }, 'Bring wrestlers over from an older SmackDown vs. Raw game. The launcher converts the model and textures so they work in this game and adds them to an empty slot. Your game files are never changed: everything goes into the mods folder and can be removed again.'));
    if (!d) { box.append(h('div', { class: 'empty' }, 'Loading...')); return; }
    if (d.error) { box.append(h('div', { class: 'note-box warn' }, d.error)); return; }
    box.append(h('div', { class: 'note-box' }, 'Imports are made for ' + d.build + '. Models and names only for now: entrances, Titantrons, music, select portraits and entrance motion are not imported yet.'));
    box.append(h('div', { class: 'field', style: 'margin-top:10px' },
      h('button', { class: 'btn pri sm', onclick: async () => { const r = await api('/api/chars', { op: 'browse' }); if (r.path) load(); } }, d.src ? 'Change source game...' : 'Choose source game folder...'),
      h('span', { class: 'lead', style: 'margin-left:10px' }, d.src ? d.src : 'Pick the extracted game folder that contains PAC\\CH.PAC (SvR 2005 works; other games are untested).')));
    if (d.source_error) box.append(h('div', { class: 'note-box warn' }, d.source_error));
    const imported = d.imported || [];
    if (imported.length) box.append(h('div', { class: 'list', style: 'margin-top:14px' }, imported.map(m => h('div', { class: 'li', style: 'grid-template-columns:minmax(0,1fr) auto' },
      h('div', {}, h('b', {}, m.full), h('p', {}, 'Slot ' + m.dst + ' - from source slot ' + m.src)),
      h('button', { class: 'btn sm', disabled: state.busy, onclick: () => run({ op: 'remove', dst: m.dst }, 'Removing ' + m.full + '...') }, 'Remove')))));
    const taken = new Set(imported.map(m => m.dst));
    const free = (d.free || []).filter(x => !taken.has(x));
    const src = (d.source || []).filter(x => !(d.free || []).includes(x.slot) || true);
    if (src.length) {
      const have = new Set(imported.map(m => m.src + ':' + m.dst));
      box.append(h('h3', { style: 'margin:16px 0 6px' }, 'Available in the source game'),
        h('p', { class: 'lead' }, 'Pick a free slot in this game for each wrestler. Slots that hold a cut wrestler\'s leftover data show first.'),
        h('div', { class: 'list' }, src.map(x => {
          const sel = h('select', { class: 'sel' }, free.map(f => h('option', { value: f }, 'Slot ' + f)));
          return h('div', { class: 'li', style: 'grid-template-columns:minmax(0,1fr) auto auto' },
            h('div', {}, h('b', {}, x.name), h('p', {}, 'Source slot ' + x.slot + (x.hasHigh ? ' - high-res set found' : ''))),
            sel,
            h('button', { class: 'btn sm', disabled: state.busy || !free.length, onclick: () => promptDialog('Name in game', 'Full name shown in the game for ' + x.name + '.', x.name, 'Import', full => {
              if (!full) return; run({ op: 'import', src: x.slot, dst: Number(sel.value), full, short: full.split(' ')[0] }, 'Converting ' + full + '... this takes a minute.'); }) }, 'Import'));
        })));
    } else if (d.src && !d.source_error) box.append(h('div', { class: 'empty' }, 'No wrestlers found in that folder.'));
  };
  const load = async () => { state.d = await api('/api/chars', { op: 'state' }); draw(); };
  const run = async (body, msg) => { state.busy = true; toast(msg); draw(); const r = await api('/api/chars', body); state.busy = false;
    toast(r.ok ? 'Done. Restart the game to see the change.' : (r.error || 'Failed'), r.ok ? undefined : 'bad'); load(); };
  load();
  return box;
}

function entrancesSection() {
  const box = h('section', { class: 'sec' });
  const st = { d: null };
  const tg = S.schema.settings.filter(x => x.section === 'Custom entrances');
  const draw = () => {
    const d = st.d;
    box.replaceChildren(h('h2', {}, 'Custom entrances'), h('p', { class: 'lead' }, 'Give any wrestler their own entrance song and Titantron. The game\'s own slots and music stay untouched; this list is separate and works for every wrestler in the registry, including ones you import. Wrestlers that are not in the game yet keep their choice and it applies as soon as they are imported.'));
    tg.forEach(x => box.append(settingRow(x)));
    if (!d) { box.append(h('div', { class: 'empty' }, 'Loading...')); return; }
    box.append(h('div', { class: 'field', style: 'margin:8px 0' },
      h('button', { class: 'btn sm', onclick: () => act('open', 'entsongs') }, 'Open songs folder'),
      h('button', { class: 'btn sm', onclick: () => act('open', 'enttitans') }, 'Open Titantrons folder'),
      h('span', { class: 'lead', style: 'margin-left:10px' }, d.songs.length + ' songs, ' + d.videos.length + ' videos found')));
    for (const g of ['Legend', 'Superstar', 'Diva']) {
      const ps = d.people.filter(x => x.group === g);
      if (!ps.length) continue;
      const det = h('details', { style: 'margin-top:8px' }, h('summary', {}, g + 's (' + ps.length + ')'));
      det.append(h('div', { class: 'li', style: 'grid-template-columns:minmax(0,1fr) repeat(3,minmax(0,1fr));opacity:.85' },
        h('div', {}, h('b', {}, 'Wrestler')),
        h('div', {}, h('b', {}, '1. Entrance song'), h('p', {}, 'Music when they walk out')),
        h('div', {}, h('b', {}, '2. Titantron'), h('p', {}, 'Video on the entrance screen')),
        h('div', {}, h('b', {}, '3. Move-set'), h('p', {}, 'Moves copied from an older game'))));
      det.append(h('div', { class: 'list' }, ps.map(x => {
        const GN = { svr05: 'SmackDown! vs. Raw (2005)', hctp: 'Here Comes the Pain' };
        const mk = (items, val, key, sug) => {
          const own = (sug || []).filter(f => items.includes(f)); const rest = items.filter(f => !own.includes(f));
          const stockLbl = key === 'song' ? 'Stock song (game default)' : key === 'video' ? 'Stock Titantron (game default)' : 'Stock moves (game default)';
          const tip = key === 'song' ? 'ENTRANCE SONG: the music that plays when this wrestler walks out. Entries marked * are this wrestler\'s own songs found in your Custom Music/Entrances folder.'
            : key === 'video' ? 'TITANTRON: the video shown on the entrance screen. Entries marked * are this wrestler\'s own videos found in your Custom Titantrons folder.'
            : 'MOVE-SET: copy this wrestler\'s moves from an older game (the game shown here) instead of the stock SvR 2006 moves. Only games that had this wrestler are listed.';
          const sel = h('select', { class: 'sel', title: tip }, [h('option', { value: '' }, stockLbl)].concat(own.map(f => h('option', { value: f }, '* ' + f)), rest.map(f => h('option', { value: f }, key === 'moveGame' ? (GN[f] || f) : f))));
          sel.value = val || '';
          sel.onchange = async () => { const r = await api('/api/entrances', Object.assign({ op: 'set', cid: x.cid, song: x.song, video: x.video, moveGame: x.moveGame, moveDonor: x.moveDonor }, { [key]: sel.value })); st.d = r; toast('Saved'); };
          return sel;
        };
        return h('div', { class: 'li', style: 'grid-template-columns:minmax(0,1fr) repeat(3,minmax(0,1fr))' },
          h('div', {}, h('b', {}, x.name), h('p', {}, x.slot == null ? 'Not in the game yet' : 'Slot ' + x.slot)),
          mk(d.songs, x.song, 'song', x.sugSongs), mk(d.videos, x.video, 'video', x.sugVideos), mk(x.moveGames, x.moveGame, 'moveGame'));
      })));
      box.append(det);
    }
  };
  (async () => { st.d = await api('/api/entrances', { op: 'state' }); draw(); })();
  draw();
  return box;
}

// ---- mods
function pageMods() {
  const w = h('div', { class: 'wrap' }); const H = heavy(); const tp = H.texpack || {}; const lr = H.last_run;
  const tex = S.schema.settings.filter(s => s.section === 'HD textures');
  const status = h('div', { class: 'note-box ' + (tp.count ? '' : 'warn') }, tp.count
    ? `The pack for this version has ${tp.count.toLocaleString()} images in ${tp.path}.` + (lr && lr.texpack_on ? ` The last run indexed ${lr.indexed.toLocaleString()} textures and swapped in ${lr.exact.toLocaleString()}.` : '')
    : `No texture pack found. Put a PCSX2-style replacement folder at ${tp.path || 'mods\\texturepack'} and it is picked up on the next start.`);
  const sec1 = section('HD textures', 'Higher resolution replacements for the game\'s own textures. Nothing in the game files is changed.', tex.map(s => settingRow(s)));
  sec1.append(status, h('div', { class: 'field', style: 'margin-top:10px' }, h('button', { class: 'btn sm', onclick: () => act('open', 'texpack') }, 'Open texture folder')));
  w.append(sec1);
  w.append(charactersSection());
  w.append(entrancesSection());
  const mus = S.schema.settings.filter(s => s.section === 'Custom music');
  const sec2 = section('Custom music', 'Replace stock songs with your own versions. Works for the PAL and USA versions.', mus.map(s => settingRow(s)));
  sec2.append(h('div', { class: 'field', style: 'margin-top:10px' }, h('button', { class: 'btn sm', onclick: () => act('open', 'mods') }, 'Open mods folder')));
  w.append(sec2);
  w.append(h('section', { class: 'sec' }, h('h2', {}, 'Button icons'), h('p', { class: 'lead' }, 'Xbox, PlayStation and Switch button prompts are chosen on the Controls page.'),
    h('div', { class: 'field', style: 'margin-top:10px' }, h('button', { class: 'btn sm', onclick: () => go('controls') }, 'Go to button icons'), h('button', { class: 'btn sm', onclick: () => act('open', 'icons') }, 'Open icon folder'))));
  return w;
}

// ---- saves
function pageSaves() {
  const C = heavy().cards || { items: [] }; const w = h('div', { class: 'wrap' });
  const refresh = async () => { S.snap = await api('/api/snapshot'); buildPage(); };
  const call = async (name, arg) => { const r = await act(name, arg); setTimeout(refresh, 400); return r; };
  w.append(h('div', { class: 'note-box', style: 'margin-top:10px' }, 'Memory cards are per version. The launcher copies your active card to a backup before every launch and keeps the ten newest. Nothing is ever deleted. Close the game before switching cards.'));
  const bar = h('div', { class: 'field', style: 'margin-top:14px' },
    h('button', { class: 'btn pri sm', onclick: () => promptDialog('New blank card', 'Give the new memory card a name.', '', 'Create', v => call('newcard', v)) }, 'New blank card'),
    h('button', { class: 'btn sm', onclick: () => call('cardimport', 'folder') }, 'Import a card folder'),
    h('button', { class: 'btn sm', onclick: () => call('cardimport', 'file') }, 'Import a PCSX2 card file'),
    h('button', { class: 'btn sm', onclick: () => act('cardopen', '') }, 'Open cards folder'));
  w.append(bar);
  if (!C.items.length) { w.append(h('div', { class: 'empty' }, 'No memory cards yet. Link the game files for this version on the Library page, or create a blank card above.')); return w; }
  w.append(h('div', { class: 'sec' }, h('h2', {}, 'Cards'), h('div', { class: 'list' }, C.items.map(c => {
    const active = c.name === C.current;
    return h('div', { class: 'li', style: 'grid-template-columns:auto minmax(0,1fr) auto' }, h('i', { class: 'st ' + (active ? 'ok' : '') }),
      h('div', {}, h('b', {}, c.name, active ? ' ' : '', active ? h('span', { class: 'tag good' }, 'In use') : null),
        h('p', {}, c.saves.length ? c.saves.length + ' save' + (c.saves.length > 1 ? 's' : '') + ' - ' + (c.kb / 1024).toFixed(1) + ' MB' : 'Empty'),
        c.saves.length ? h('p', { style: 'margin-top:2px' }, c.saves.map(s => s.dir).join(', ')) : null,
        c.backups.length ? h('p', { style: 'margin-top:4px' }, h('button', { class: 'link', onclick: () => restoreDialog(c, call) }, c.backups.length + ' backup' + (c.backups.length > 1 ? 's' : '') + ' - restore one')) : null),
      h('div', { style: 'display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end' },
        active ? null : h('button', { class: 'btn sm', onclick: () => call('cardsel', c.name) }, 'Use this card'),
        h('button', { class: 'btn sm', onclick: () => promptDialog('Rename card', 'New name for "' + c.name + '".', c.name === 'Default' ? '' : c.name, 'Rename', v => call('cardrename', c.name + '\n' + v)) }, 'Rename'),
        h('button', { class: 'btn sm', onclick: () => act('cardopen', c.name) }, 'Open')));
  }))));
  return w;
}
function restoreDialog(c, call) {
  openModal(h('div', { class: 'dlg' }, h('header', {}, h('h3', {}, 'Restore a backup'), h('p', {}, 'A restore never overwrites: it becomes a new card you can switch to.')),
    h('div', { class: 'body' }, h('div', { class: 'list' }, c.backups.map(b => h('div', { class: 'li' }, h('i', { class: 'st' }), h('div', {}, h('b', {}, b.replace(/^(\d{4})(\d\d)(\d\d)_(\d\d)(\d\d)(\d\d)$/, '$1-$2-$3  $4:$5'))),
      h('button', { class: 'btn sm', onclick: () => { closeModal(); call('cardrestore', c.name + '\n' + b); } }, 'Restore as new card'))))),
    h('footer', {}, h('button', { class: 'btn', onclick: closeModal }, 'Close'))));
}

// ---- library
function pageLibrary() {
  const L = heavy().library || { ps2: [], psp: [] }; const w = h('div', { class: 'wrap' });
  const refresh = async () => { S.snap = await api('/api/snapshot'); buildPage(); };
  const op = async (o, slot, path) => { const r = await api('/api/games', { op: o, slot, path }); if (r.message) toast(r.message); await refresh(); return r; };
  const slotRow = d => h('div', { class: 'li', style: 'grid-template-columns:auto minmax(0,1fr) auto' }, h('i', { class: 'st ' + (d.found ? (d.supported === false ? 'warn' : 'ok') : '') }),
    h('div', {}, h('b', {}, d.name, ' ', h('span', { class: 'tag' }, d.region)),
      h('p', {}, d.found ? (d.serial || d.disc_id || 'Recognised') + (d.supported === false ? ' - linked for reference, not playable yet' : '') : 'Not linked. ' + (d.sub || '')),
      d.path ? h('p', { style: 'margin-top:2px;color:var(--faint)' }, d.path + (d.auto ? '  (found automatically)' : '')) : null,
      d.features && d.features.length ? h('p', { style: 'margin-top:6px;display:flex;gap:6px;flex-wrap:wrap' }, d.features.map(f => h('span', { class: 'tag ' + (f.ok ? 'good' : 'dev') }, f.name || f.id))) : null),
    h('div', { style: 'display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end' },
      h('button', { class: 'btn sm', onclick: async () => { const r = await api('/api/games', { op: 'browse', slot: d.slot_id }); if (r.path) op('link', d.slot_id, r.path); } }, d.found ? 'Change...' : 'Link...'),
      d.linked ? h('button', { class: 'btn sm', onclick: () => op('unlink', d.slot_id) }, 'Unlink') : null,
      d.slot_id.startsWith('psp') && d.found && d.features && d.features.length && L.psp_primary !== d.slot_id ? h('button', { class: 'btn sm', onclick: () => op('primary', d.slot_id) }, 'Use for extras') : null,
      d.slot_id === L.psp_primary ? h('span', { class: 'tag acc' }, 'Used for extras') : null));
  w.append(h('section', { class: 'sec' }, h('h2', {}, 'PlayStation 2 games'), h('p', { class: 'lead' }, 'Point each version at the folder that holds its boot file. The launcher also looks in your assets folder by itself.'), h('div', { class: 'list' }, L.ps2.map(slotRow))));
  w.append(h('section', { class: 'sec' }, h('h2', {}, 'PSP games'), h('p', { class: 'lead' }, 'The PSP version supplies extra game modes (Game Show, Eugene, Hold\'em) and data.'), h('div', { class: 'list' }, L.psp.map(slotRow))));
  w.append(h('div', { style: 'margin-top:18px' }, h('button', { class: 'btn sm', onclick: () => op('rescan', '') }, 'Scan again')));
  return w;
}

// ---- tools
function pageTools() {
  const w = h('div', { class: 'wrap' }); const tabs = ['Health check', 'Logs', 'Screenshots', 'Support']; let cur = S.toolTab || 0;
  const body = h('div');
  const seg = h('div', { class: 'seg', style: 'margin-top:10px' }, tabs.map((t, i) => h('button', { type: 'button', 'aria-pressed': String(i === cur), onclick: e => { S.toolTab = i; cur = i; [...seg.children].forEach((b, j) => b.setAttribute('aria-pressed', String(j === i))); draw(); } }, t)));
  function draw() { body.replaceChildren(); [toolHealth, toolLogs, toolShots, toolSupport][cur](body); }
  w.append(seg, body); draw(); return w;
}
function toolHealth(box) {
  const list = h('div', { class: 'list' }, h('p', { class: 'desc' }, 'Checking...')); box.append(h('section', { class: 'sec' }, h('h2', {}, 'Health check'),
    h('p', { class: 'lead' }, 'Red means something blocks the game. Amber is optional or informational.'), list, h('div', { style: 'margin-top:12px' }, h('button', { class: 'btn sm', onclick: async () => { list.replaceChildren(h('p', { class: 'desc' }, 'Checking...')); await loadHealth(true); draw(); } }, 'Check again'))));
  const draw = () => list.replaceChildren(...(S.health || []).map(x => h('div', { class: 'li' }, h('i', { class: 'st ' + (x.ok === true ? 'ok' : x.ok === false ? 'bad' : 'warn') }), h('div', {}, h('b', {}, x.title), x.detail ? h('p', {}, x.detail) : null, x.fix ? h('p', { style: 'color:var(--warn)' }, x.fix) : null), null)));
  loadHealth().then(draw);
}
function toolLogs(box) {
  const out = h('pre', { class: 'log' }, 'Loading...'); const info = h('p', { class: 'desc' });
  const srcSeg = h('div', { class: 'seg' }, [['run_err', 'Game'], ['run_out', 'Game output'], ['build', 'Build']].map(([id, t]) => h('button', { type: 'button', 'aria-pressed': String(S.logsrc === id), onclick: e => { S.logsrc = id; [...srcSeg.children].forEach(b => b.setAttribute('aria-pressed', String(b === e.target))); load(); } }, t)));
  const f = h('input', { type: 'text', class: 'sel', placeholder: 'Filter lines (separate words with |)', value: S.logfilter, style: 'min-width:240px;flex:1' });
  let t; f.oninput = () => { S.logfilter = f.value; clearTimeout(t); t = setTimeout(load, 350); };
  async function load() {
    try { const r = await api('/api/log?src=' + S.logsrc + '&f=' + encodeURIComponent(S.logfilter) + '&ver=' + sel());
      info.textContent = r.size ? `${r.matched} of ${r.total} lines, showing the last ${r.lines.length}` : 'No log yet for this version.';
      out.replaceChildren(...r.lines.map(l => h('div', { class: /error|fatal|assert|exception/i.test(l) ? 'e' : /warn/i.test(l) ? 'w' : '' }, l)));
      out.scrollTop = out.scrollHeight; } catch (e) { out.textContent = e.message; } }
  box.append(h('section', { class: 'sec' }, h('h2', {}, 'Logs'), h('div', { class: 'field', style: 'margin-top:10px' }, srcSeg, f, h('button', { class: 'btn sm', onclick: load }, 'Refresh'), h('button', { class: 'btn sm', onclick: () => act('open', 'logs') }, 'Open folder')), info, out));
  load();
}
function toolShots(box) {
  const g = h('div', { class: 'shots' });
  box.append(h('section', { class: 'sec' }, h('h2', {}, 'Screenshots'), h('p', { class: 'lead' }, 'Taken with the Screenshot button on the Play page while a game runs.'), g,
    h('div', { style: 'margin-top:12px' }, h('button', { class: 'btn sm', onclick: () => act('open', 'shots') }, 'Open folder'))));
  api('/api/shots').then(sh => g.replaceChildren(...(sh.length ? sh.map(s => h('a', { href: '/file?p=' + encodeURIComponent(s.path) + '&t=' + window.TOKEN, target: '_blank' }, h('img', { loading: 'lazy', src: '/file?p=' + encodeURIComponent(s.path) + '&t=' + window.TOKEN, alt: '' }), h('span', {}, s.name))) : [h('div', { class: 'empty' }, 'No screenshots yet.')])));
}
function toolSupport(box) {
  const H = heavy();
  box.append(section('Report a problem', 'Bundles the logs, settings and your latest screenshots into one zip you can attach to a bug report.', h('div', { class: 'row block' }, h('div', { class: 'field' },
    h('button', { class: 'btn pri sm', onclick: () => act('report', '') }, 'Create bug report zip'), h('button', { class: 'btn sm', onclick: () => act('open', 'logs') }, 'Open logs folder')))));
  box.append(section('Housekeeping', '', h('div', { class: 'row block' }, h('div', { class: 'field' },
    h('button', { class: 'btn sm', onclick: () => confirm('Delete all but the newest 20 sets of old run logs?') && act('prune', '') }, 'Clean up old logs'),
    h('button', { class: 'btn sm', onclick: () => act('open', 'root') }, 'Open project folder'), h('button', { class: 'btn sm', onclick: () => act('open', 'changelog') }, 'Open change log')))));
  if ((H.scenarios || []).length) box.append(section('Test scenarios', 'Scripted checks that drive a running game.', h('div', { class: 'row block' }, h('div', { class: 'field' }, H.scenarios.map(s => h('button', { class: 'btn sm', onclick: () => act('scenario', s) }, s))))));
}

// ---- launcher settings
function pageLauncher() {
  const w = h('div', { class: 'wrap' }); const U = S.ui;
  const mkSeg = (key, opts) => { const bs = opts.map(([v, t]) => h('button', { type: 'button', 'aria-pressed': String(U[key] === v), onclick: e => { setUi({ [key]: v }); [...bs].forEach(b => b.setAttribute('aria-pressed', String(b === e.target))); } }, t)); return h('div', { class: 'seg' }, bs); };
  const row = (l, d, c, block) => h('div', { class: 'row' + (block ? ' block' : '') }, h('div', {}, h('div', { class: 'lab' }, l), h('p', { class: 'desc' }, d)), h('div', { class: 'ctl' }, c));
  const themes = h('div', { class: 'themes' }, S.schema.themes.map(t => {
    const pal = { graphite: ['#0a0c0f', '#14171c'], midnight: ['#070a14', '#10162a'], ringside: ['#0d0b09', '#1a1713'], canvas: ['#e9ebf0', '#ffffff'], system: ['#0a0c0f', '#ffffff'] }[t.id];
    return h('button', { type: 'button', class: 'theme', 'aria-pressed': String(U.theme === t.id), onclick: e => { setUi({ theme: t.id }); [...themes.children].forEach(b => b.setAttribute('aria-pressed', 'false')); e.currentTarget.setAttribute('aria-pressed', 'true'); } },
      h('div', { class: 'pv', style: `background:${pal[1]};--pa:var(--accent)` }, h('i', { style: 'background:' + pal[0] }), h('i')), h('b', {}, t.name), h('span', {}, t.sub));
  }));
  const sw = h('div', { class: 'swatches' }, S.schema.accents.map(a => h('button', { type: 'button', class: 'swatch', title: a.name, 'aria-label': a.name, 'aria-pressed': String(U.accent === a.id), style: 'background:' + a.color, onclick: e => { setUi({ accent: a.id }); [...sw.children].forEach(b => b.setAttribute('aria-pressed', 'false')); e.currentTarget.setAttribute('aria-pressed', 'true'); }, html: ic('check') })));
  const sz = h('input', { type: 'range', min: 85, max: 140, step: 5, value: U.textScale, 'aria-label': 'Text size' }); const so = h('output', {}, U.textScale + '%');
  const paint = () => { sz.style.setProperty('--p', ((sz.value - 85) / 55 * 100) + '%'); so.textContent = sz.value + '%'; paint.last = 1; }; paint();
  sz.oninput = paint; sz.onchange = () => setUi({ textScale: +sz.value });
  w.append(section('Look', 'Everything here changes only this launcher, not the game.',
    row('Theme', 'Dark themes are easiest on the eyes in a dim room. Match Windows follows your system light or dark setting.', themes, true),
    row('Accent color', 'Used for buttons, switches and highlights.', sw),
    row('Text size', 'Makes every label in the launcher larger or smaller. Text always wraps, so nothing is cut off.', h('div', { class: 'rng' }, sz, so)),
    row('Spacing', 'Compact fits more settings on screen.', mkSeg('density', [['comfortable', 'Comfortable'], ['compact', 'Compact']])),
    row('Motion', 'Reduced turns off fades and slides.', mkSeg('motion', [['full', 'Full'], ['reduced', 'Reduced']]))));
  w.append(section('Behavior', '',
    row('Open on', 'The page the launcher shows first.', (() => { const s = h('select', { class: 'sel' }, S.schema.pages.map(p => h('option', { value: p.id }, p.name))); s.value = U.startPage; s.onchange = () => setUi({ startPage: s.value }); return s; })()),
    row('After starting the game', 'Stay open keeps the launcher ready for cheats and screenshots. Minimize gets it out of the way.', mkSeg('afterLaunch', [['stay', 'Stay open'], ['minimize', 'Minimize']]))));
  w.append(section('About', '', h('div', { class: 'row block' }, h('div', {}, h('p', { class: 'desc' }, 'SvR06 Launcher 2. Windows snap (drag to an edge, or Win + arrow keys) and the usual minimize, maximize and close buttons come from Windows itself. Your window size and position are remembered.'),
    h('p', { class: 'desc', style: 'margin-top:6px' }, 'Project folder: ' + (heavy().root || '')), h('div', { class: 'field', style: 'margin-top:10px' },
      h('button', { class: 'btn sm', onclick: () => act('open', 'root') }, 'Open project folder'), h('button', { class: 'btn sm danger', onclick: () => confirm('Close the launcher?') && api('/api/exit', {}).catch(() => 0).then(() => window.close()) }, 'Close launcher'))))));
  return w;
}

// ---------------------------------------------------------------- search
function searchResults(q) {
  const t = q.toLowerCase().split(/\s+/).filter(Boolean); const w = h('div', { class: 'wrap results' });
  const hit = s => { const hay = (s.label + ' ' + s.desc + ' ' + s.detail + ' ' + s.section + ' ' + s.page).toLowerCase(); return t.every(x => hay.includes(x)); };
  const res = S.schema.settings.filter(hit);
  if (!res.length) { w.append(h('div', { class: 'empty' }, 'Nothing matches "' + q + '". Try a shorter word, like "sharp" or "volume".')); return w; }
  const names = Object.fromEntries(S.schema.pages.map(p => [p.id, p.name]));
  const pages = [...new Set(res.map(s => s.page))];
  for (const p of pages) { w.append(h('p', { class: 'grp' }, names[p])); w.append(h('div', { class: 'rows' }, res.filter(s => s.page === p).map(s => settingRow(s)))); }
  return w;
}

// ---------------------------------------------------------------- shell
const PAGES = { play: pagePlay, graphics: pageGraphics, audio: pageAudio, controls: pageControls, gameplay: pageGameplay, cheats: pageCheats, mods: pageMods, saves: pageSaves, library: pageLibrary, tools: pageTools, launcher: pageLauncher };

function buildRail() {
  const rail = $('#rail'); rail.replaceChildren(h('div', { class: 'brand' }, h('div', { class: 'mark' }, 'S6'), h('div', {}, h('b', {}, 'SvR06'), h('small', {}, 'PC port launcher'))));
  let grp = null;
  for (const p of S.schema.pages) {
    if (p.group && p.group !== grp) { grp = p.group; rail.append(h('div', { class: 'nav-group' }, grp)); }
    rail.append(h('button', { type: 'button', class: 'nav', dataset: { page: p.id }, title: p.name, 'aria-current': p.id === S.page ? 'page' : null, onclick: () => go(p.id), html: ic(p.id === 'play' ? 'play' : p.id) + '<span>' + p.name + '</span>' }));
  }
  rail.append(h('div', { class: 'rail-foot' }, h('i', { class: 'pulse', id: 'railDot' }), h('span', { id: 'railTxt' }, 'Game closed')));
}
function go(id) { S.page = id; S.query = ''; $('#q').value = ''; buildPage(); $('#main').scrollTop = 0; }
function buildPage() {
  S.syncs = {}; S.resets = {};
  document.querySelectorAll('.nav').forEach(n => n.setAttribute('aria-current', !S.query && n.dataset.page === S.page ? 'page' : 'false'));
  const pg = S.schema.pages.find(p => p.id === S.page) || S.schema.pages[0]; const main = $('#main');
  if (S.query) { $('#pageTitle').textContent = 'Search'; $('#pageSub').textContent = 'Results across every page'; main.replaceChildren(searchResults(S.query)); }
  else { $('#pageTitle').textContent = pg.name; $('#pageSub').textContent = pg.sub || (pg.id === 'play' ? 'Start the game, switch version, check on things' : ''); main.replaceChildren(PAGES[pg.id]()); }
  document.title = (S.query ? 'Search' : pg.name) + ' - SvR06 Launcher';
  syncAll();
}

function pageSig() {
  const H = heavy(); const L = H.library || {}; const C = H.cards || {};
  return [S.page, sel(), (H.builds || []).map(b => b.id + b.built + b.exe_mtime).join(), JSON.stringify(C.items ? C.items.map(c => [c.name, c.kb, c.backups.length]) : ''), C.current,
    JSON.stringify((L.ps2 || []).concat(L.psp || []).map(d => [d.found, d.path])), JSON.stringify((H.cheats || {}).defs ? 1 : 0)].join('#');
}
async function poll() {
  try {
    const sn = await api('/api/snapshot'); const first = !S.snap; S.snap = sn;
    if (first) { applyUi(sn.ui); S.page = S.ui.startPage || 'play'; buildRail(); buildPage(); S.sig = pageSig(); }
    else {
      const sig = pageSig();
      const active = document.activeElement && document.activeElement.closest('#main') && ['INPUT', 'SELECT'].includes(document.activeElement.tagName) && document.activeElement.type !== 'range' && document.activeElement.type !== 'checkbox';
      if (sig !== S.sig && !S.query && !active && !S.dragging && $('#modal').hidden && ['play', 'saves', 'library'].includes(S.page)) { S.sig = sig; buildPage(); }
      else { S.sig = sig; if (!S.dragging) syncAll(); }
    }
  } catch (e) { /* launcher backend restarting; try again */ }
  setTimeout(poll, 1500);
}

async function boot() {
  const sc = await api('/api/schema'); sc.byKey = Object.fromEntries(sc.settings.map(s => [s.key, s])); S.schema = sc;
  const q = $('#q'); let t;
  q.addEventListener('input', () => { clearTimeout(t); t = setTimeout(() => { S.query = q.value.trim(); buildPage(); }, 120); });
  addEventListener('keydown', e => {
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') { e.preventDefault(); q.focus(); q.select(); }
    else if (e.key === 'Escape') { if (!$('#modal').hidden) closeModal(); else if (q.value) { q.value = ''; S.query = ''; buildPage(); } }
  });
  poll();
}
boot().catch(e => { document.body.append(h('pre', { style: 'padding:30px;color:#f77' }, 'The launcher UI could not start: ' + e.message)); });
