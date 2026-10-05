(() => {
'use strict';

/* ------------------------------------------------------------------ helpers */
const $  = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const esc = s => String(s ?? '').replace(/[&<>"]/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const fmtMs = v => (v < 10 ? v.toFixed(1) : Math.round(v)) + ' ms';
const CHEV = '<svg viewBox="0 0 24 24"><path d="M6 9l6 6 6-6"/></svg>';

const S = { settings: {}, dev: null, chain: [], plugins: [], scan: { running: false, folders: [] }, stats: null, page: 0 };

/* ------------------------------------------------------------------- bridge */
let bridge;
function boot() {
  bridge = window.__JUCE__.backend;
  const on = (id, fn) => bridge.addEventListener(id, fn);
  on('settings', onSettings); on('devices', onDevices); on('chain', onChain);
  on('plugins', onPlugins);   on('scan', onScan);       on('stats', onStats);
  on('meters', onMeters);     on('toast', t => toast(t.kind, t.text));
  init();
  send('ready');
}
const send = (c, d = {}) => bridge.emitEvent('cmd', Object.assign({ c }, d));

if (window.__JUCE__ && window.__JUCE__.backend) boot();
else { const s = document.createElement('script'); s.src = 'dev/mock.js'; s.onload = boot; s.onerror = () => toast('error', 'No native backend found.'); document.head.appendChild(s); }

/* ------------------------------------------------------------------- toasts */
function toast(kind, text) {
  if (!text) return;
  const t = document.createElement('div');
  t.className = 'toast ' + (kind || '');
  t.textContent = text;
  t.onclick = () => t.remove();
  $('#toasts').appendChild(t);
  if (kind !== 'error') setTimeout(() => t.remove(), kind === 'warn' ? 9000 : 4500);
  while ($('#toasts').children.length > 4) $('#toasts').firstChild.remove();
}

/* --------------------------------------------------------------------- tabs */
function setSeg(seg, index, n) { seg.style.setProperty('--i', index); seg.style.setProperty('--n', n); }

function init() {
  const tabs = $('#tabs');
  $$('button', tabs).forEach((b, i) => {
    b.onclick = () => showPage(i);
  });
  showPage(+localStorage.getItem('velo.page') || 0);
  document.addEventListener('contextmenu', e => { if (e.target.tagName !== 'INPUT') e.preventDefault(); });

  // switches
  $$('.tile').forEach(t => t.onclick = () => {
    const on = t.getAttribute('aria-checked') !== 'true';
    S.settings[t.dataset.sw] = on; paintSwitches();
    send('switch', { k: t.dataset.sw, v: on });
  });

  // sliders
  $$('.strip').forEach(strip => {
    const k = strip.dataset.k, r = $('input', strip), out = $('output', strip);
    const min = +strip.dataset.min, max = +strip.dataset.max;
    let queued = null, last = 0;
    const paint = () => {
      const v = +r.value;
      r.style.setProperty('--p', ((v - min) / (max - min) * 100) + '%');
      out.textContent = v <= min + 0.05 ? 'Off' : v.toFixed(1) + ' dB';
    };
    strip._paint = v => { r.value = v; paint(); };
    r.oninput = () => {
      paint();
      const now = performance.now();
      if (now - last > 30) { last = now; send('gain', { k, db: +r.value }); }
      else { clearTimeout(queued); queued = setTimeout(() => { last = performance.now(); send('gain', { k, db: +r.value }); }, 35); }
    };
    r.onchange = () => { clearTimeout(queued); send('gain', { k, db: +r.value }); };
    r.ondblclick = () => { r.value = 0; paint(); send('gain', { k, db: 0 }); };
    paint();
  });

  buildLatencyCards();
  startMeters();
  initFlow();

  $('#btnPanel').onclick   = () => send('panel');
  $('#btnRefresh').onclick = () => send('rescanDevices');
  $('#btnScan').onclick    = () => send(S.scan.running ? 'scanStop' : 'scan', { full: false });
  $('#btnMore').onclick    = e => scanMenu(e.currentTarget);
  $('#btnAdd').onclick     = () => send('addSlot');
  initPicker();
  document.addEventListener('pointerdown', e => {
    if (!e.target.closest('.select')) $$('.select.open').forEach(s => s.classList.remove('open'));
    if (!e.target.closest('.menu') && !e.target.closest('#btnMore')) $('#menu').hidden = true;
  });
}

function showPage(i) {
  S.page = i;
  try { localStorage.setItem('velo.page', i); } catch (e) {}
  setSeg($('#tabs'), i, 3);
  $$('#tabs button').forEach((b, k) => b.setAttribute('aria-selected', k === i));
  $$('.page').forEach((p, k) => p.hidden = k !== i);
}


/* -------------------------------------------------------------- signal path */
// The memorable element: the actual signal drawn as voice-bars travelling along the wires
// mic -> effects -> headphones / virtual cable. Bars are the live meter history of each stage.
const flow = { cv: null, ctx: null, W: 0, H: 196, hist: { in: [], mon: [], virt: [] }, acc: 0, last: 0, pulse: 0 };
const COL = { in: '#6db5ff', mon: '#a78bfa', virt: '#5ee0c1', amber: '#ffb020', dim: 'rgba(255,255,255,.16)' };

function initFlow() {
  const cv = flow.cv = $('#flow'); flow.ctx = cv.getContext('2d');
  const fit = () => {
    const dpr = window.devicePixelRatio || 1, w = cv.clientWidth;
    if (!w) return;
    flow.W = w; cv.width = Math.round(w * dpr); cv.height = Math.round(flow.H * dpr);
    flow.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  };
  new ResizeObserver(fit).observe(cv); fit();
  for (const k of ['in', 'mon', 'virt']) flow.hist[k] = new Array(160).fill(0);
  requestAnimationFrame(drawFlow);
}

function flowState() {
  const s = S.settings, st = S.stats;
  const fxOn = s.fx !== false;
  const active = S.chain.filter(c => c.name && !c.bypassed).length;
  const loaded = S.chain.filter(c => c.name).length;
  return {
    mic: !!s.mic, mon: !!s.mon, fxOn, active, loaded,
    gain: s.gainIn ?? 0,
    hp: st && st.hp.valid ? st.hp : null, v: st && st.v.valid ? st.v : null,
    fxMs: st && st.hp.valid ? st.hp.fx : 0
  };
}

function rr(ctx, x, y, w, h, r) { ctx.beginPath(); ctx.roundRect(x, y, w, h, r); }

function node(ctx, x, y, w, h, title, value, color, on) {
  ctx.save();
  if (on) { ctx.shadowColor = color; ctx.shadowBlur = 18; }
  rr(ctx, x, y, w, h, 20);
  ctx.fillStyle = 'rgba(7,11,24,.72)'; ctx.fill();
  ctx.shadowBlur = 0;
  ctx.lineWidth = 1.2; ctx.strokeStyle = on ? color : 'rgba(255,255,255,.14)'; ctx.globalAlpha = on ? .75 : 1; ctx.stroke();
  ctx.globalAlpha = 1;
  ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
  ctx.fillStyle = '#a3aecb'; ctx.font = '600 11.5px ' + flow.font;
  ctx.fillText(title, x + w / 2, y + h * 0.31);
  ctx.fillStyle = on ? '#f5f7fc' : '#6b77a0'; ctx.font = '650 14.5px ' + flow.font;
  ctx.fillText(value, x + w / 2, y + h * 0.66, w - 10);
  ctx.restore();
}

// bars along a cubic bezier; bar 0 sits at the source and carries the newest sample
function wire(ctx, p0, p1, p2, p3, hist, color, live, maxH) {
  const len = Math.hypot(p3.x - p0.x, p3.y - p0.y) * 1.08, pitch = 5.2;
  const n = Math.max(6, Math.floor(len / pitch));
  ctx.save();
  ctx.lineCap = 'round';
  for (let i = 0; i < n; i++) {
    const t = (i + .5) / n, u = 1 - t;
    const x = u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x;
    const y = u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y;
    const dx = 3*u*u*(p1.x-p0.x) + 6*u*t*(p2.x-p1.x) + 3*t*t*(p3.x-p2.x);
    const dy = 3*u*u*(p1.y-p0.y) + 6*u*t*(p2.y-p1.y) + 3*t*t*(p3.y-p2.y);
    const l = Math.hypot(dx, dy) || 1, nx = -dy / l, ny = dx / l;
    const a = live ? Math.pow(hist[hist.length - 1 - i] || 0, .75) : 0;
    const half = 1.6 + a * maxH;
    ctx.globalAlpha = live ? (1 - t * .5) * (.35 + a * .65) : .38;
    ctx.strokeStyle = live && a > .9 ? COL.amber : (live ? color : COL.dim);
    ctx.lineWidth = 2.6;
    ctx.beginPath(); ctx.moveTo(x - nx * half, y - ny * half); ctx.lineTo(x + nx * half, y + ny * half); ctx.stroke();
  }
  ctx.restore();
}

function drawFlow(now) {
  requestAnimationFrame(drawFlow);
  const ctx = flow.ctx, W = flow.W, H = flow.H;
  if (!ctx || !W || S.page !== 0 || document.hidden) return;
  flow.font = getComputedStyle(document.body).fontFamily;

  // sample at ~30 Hz so the flow speed does not depend on the display refresh rate
  const dt = Math.min(100, now - (flow.last || now)); flow.last = now; flow.acc += dt;
  while (flow.acc >= 33) {
    flow.acc -= 33;
    for (const k of ['in', 'mon', 'virt']) { const h = flow.hist[k]; h.push(lv[k] ? lv[k].d : 0); if (h.length > 160) h.shift(); }
  }

  const st = flowState();
  ctx.clearRect(0, 0, W, H);

  const nw = Math.max(80, Math.min(108, W * .2)), nh = 60, cy = H / 2;
  const PAD = 16, fxX = W * .42 - nw / 2, hpY = 22, cbY = H - 22 - nh;
  const mic = { x: PAD, y: cy - nh / 2 }, fx = { x: fxX, y: cy - nh / 2 };
  const hp = { x: W - nw - PAD, y: hpY }, cb = { x: W - nw - PAD, y: cbY };

  const flowing = st.mic;                         // nothing leaves the microphone node while it is muted
  const maxH = 17;
  wire(ctx, { x: mic.x + nw + 6, y: cy }, { x: mic.x + nw + 30, y: cy }, { x: fx.x - 30, y: cy }, { x: fx.x - 6, y: cy },
       flow.hist.in, COL.in, true, maxH);
  wire(ctx, { x: fx.x + nw + 6, y: cy }, { x: fx.x + nw + 46, y: cy }, { x: hp.x - 46, y: hp.y + nh / 2 }, { x: hp.x - 6, y: hp.y + nh / 2 },
       flow.hist.mon, COL.mon, flowing && st.mon, maxH * .8);
  wire(ctx, { x: fx.x + nw + 6, y: cy }, { x: fx.x + nw + 46, y: cy }, { x: cb.x - 46, y: cb.y + nh / 2 }, { x: cb.x - 6, y: cb.y + nh / 2 },
       flow.hist.virt, COL.virt, flowing && !!st.v, maxH * .8);

  const q = (ms, lim) => ms <= lim[0] ? '#30d158' : ms <= lim[1] ? '#6db5ff' : ms <= lim[2] ? '#ffb020' : '#ff453a';
  node(ctx, mic.x, mic.y, nw, nh, 'Microphone', st.mic ? (st.gain >= 0 ? '+' : '') + st.gain.toFixed(1) + ' dB' : 'Muted', COL.in, st.mic);
  node(ctx, fx.x, fx.y, nw, nh, 'Effects', !st.loaded ? 'Empty' : !st.fxOn ? 'Bypassed' : st.active + ' active', COL.amber, st.mic && st.fxOn && st.active > 0);
  node(ctx, hp.x, hp.y, nw, nh, 'Headphones', st.hp && st.mon ? fmtMs(st.hp.total) : 'Off', st.hp ? q(st.hp.total, [10, 20, 35]) : COL.mon, st.mic && st.mon && !!st.hp);
  node(ctx, cb.x, cb.y, nw, nh, 'Virtual cable', st.v ? fmtMs(st.v.total) : 'Idle', st.v ? q(st.v.total, [40, 80, 150]) : COL.virt, st.mic && !!st.v);
}

/* ----------------------------------------------------------------- settings */
function onSettings(s) {
  S.settings = s;
  paintSwitches();
  $$('.strip').forEach(st => st._paint(s['gain' + ({ in: 'In', mon: 'Mon', virt: 'Virt' }[st.dataset.k])] ?? 0));
}
function paintSwitches() {
  const txt = { mic: ['Muted', 'Streaming'], mon: ['Off', 'On'], fx: ['Bypassed', 'Active'] };
  $$('.tile').forEach(t => {
    const on = !!S.settings[t.dataset.sw];
    t.setAttribute('aria-checked', on);
    $('.s', t).textContent = txt[t.dataset.sw][on ? 1 : 0];
  });
}

/* ------------------------------------------------------------------- meters */
const lv = {};
function startMeters() {
  for (const k of ['in', 'mon', 'virt']) {
    const m = $(`.strip[data-k=${k}] .meter`);
    lv[k] = { t: 0, d: 0, h: 0, hf: 0, fill: $('.fill', m), peak: $('.peak', m), w: m };
  }
  const dbPos = x => clamp((20 * Math.log10(Math.max(x, 1e-6)) + 60) / 60, 0, 1);
  lv.pos = dbPos;
  const loop = () => {
    for (const k of ['in', 'mon', 'virt']) {
      const m = lv[k];
      m.d += (m.t - m.d) * (m.t > m.d ? 0.65 : 0.1);
      if (m.d < 0.002) m.d = 0;
      if (m.d >= m.h) { m.h = m.d; m.hf = 36; } else if (--m.hf <= 0) m.h = Math.max(0, m.h - 0.012);
      m.fill.style.clipPath = `inset(0 ${((1 - m.d) * 100).toFixed(2)}% 0 0 round 99px)`;
      m.peak.style.left = `calc(${(m.h * 100).toFixed(2)}% - 2px)`;
      m.peak.style.opacity = m.h > 0.02 ? .85 : 0;
    }
    requestAnimationFrame(loop);
  };
  requestAnimationFrame(loop);
}
function onMeters(m) { lv.in.t = lv.pos(m.i); lv.mon.t = lv.pos(m.m); lv.virt.t = lv.pos(m.v); }

/* ------------------------------------------------------------ latency cards */
const cards = {};
const QUAL = [['Excellent', 'var(--green)'], ['Good', 'var(--blue-hi)'], ['Noticeable', 'var(--amber)'], ['High', 'var(--red)']];

function buildLatencyCards() {
  const defs = {
    hp: { title: 'Headphones delay', limits: [10, 20, 35],
          segs: [['in', 'Input', 'var(--blue-hi)'], ['out', 'Output', 'var(--violet)'], ['fx', 'Effects', 'var(--amber)']] },
    v:  { title: 'Virtual cable delay', limits: [40, 80, 150],
          segs: [['in', 'Microphone', 'var(--blue-hi)'], ['fx', 'Effects', 'var(--amber)'], ['queue', 'Sync queue', 'var(--teal)'], ['dev', 'Cable device', 'var(--violet)']] }
  };
  for (const [key, d] of Object.entries(defs)) {
    const c = document.createElement('div');
    c.className = 'card lat off';
    c.innerHTML = `<div class="top2"><span class="name">${d.title}</span><span class="tag">Idle</span></div>
      <div class="big"><span class="num">--</span><span class="unit">ms</span></div>
      <div class="sub">Not running</div>
      <div class="stack">${d.segs.map(s => `<span style="background:${s[2]};flex-grow:0"></span>`).join('')}</div>
      <div class="legend">${d.segs.map(s => `<div><i style="background:${s[2]}"></i>${s[1]}<b>0 ms</b></div>`).join('')}</div>`;
    $('#lat').appendChild(c);
    cards[key] = { el: c, d, num: $('.num', c), unit: $('.unit', c), tag: $('.tag', c), sub: $('.sub', c),
                   bars: $$('.stack span', c), vals: $$('.legend b', c), shown: 0 };
  }
}

function updateLatency(key, data, subText) {
  const c = cards[key];
  if (!data || !data.valid) {
    c.el.classList.add('off'); c.num.textContent = '--'; c.unit.style.visibility = 'hidden';
    c.tag.textContent = 'Idle'; c.tag.style.cssText = ''; c.sub.textContent = subText || 'Not running';
    c.bars.forEach(b => b.style.flexGrow = 0); c.vals.forEach(v => v.textContent = '0 ms'); c.shown = 0;
    return;
  }
  c.el.classList.remove('off'); c.unit.style.visibility = 'visible';
  const q = c.d.limits.findIndex(l => data.total <= l);
  const [label, col] = QUAL[q < 0 ? 3 : q];
  c.num.style.color = col;
  c.tag.textContent = label; c.tag.style.background = 'rgba(255,255,255,.1)'; c.tag.style.color = col;
  c.sub.textContent = subText;
  tween(c, data.total);
  c.d.segs.forEach(([k], i) => {
    const v = data[k] || 0;
    c.bars[i].style.flexGrow = Math.max(v, 0.0001);
    c.vals[i].textContent = fmtMs(v);
  });
}

function tween(c, to) {
  const from = c.shown; c.shown = to;
  if (c.raf) cancelAnimationFrame(c.raf);
  const t0 = performance.now(), dur = 450;
  const step = now => {
    const p = clamp((now - t0) / dur, 0, 1), e = 1 - Math.pow(1 - p, 3), v = from + (to - from) * e;
    c.num.textContent = v < 10 ? v.toFixed(1) : String(Math.round(v));
    if (p < 1) c.raf = requestAnimationFrame(step);
  };
  c.raf = requestAnimationFrame(step);
}

function onStats(s) {
  S.stats = s;
  const pill = $('#status');
  pill.className = 'pill ' + (s.ok ? 'ok' : 'bad');
  $('.txt', pill).textContent = s.ok ? `${s.type} · ${(s.hp.rate / 1000).toFixed(1)} kHz · ${s.hp.buf} spl` : 'No audio device';

  const hp = s.hp, v = s.v;
  updateLatency('hp', hp, hp.valid ? `${s.type} · ${hp.buf} samples · ${(hp.rate / 1000).toFixed(1)} kHz${hp.est ? ' · estimated' : ''}` : 'Interface not running');
  updateLatency('v',  v,  v.valid  ? `${v.buf} samples · ${(v.rate / 1000).toFixed(1)} kHz · includes sync queue` : 'Virtual cable not running');

  const cpu = Math.round(s.cpu * 100);
  const chips = [
    `<span class="chip ${cpu > 85 ? 'bad' : cpu > 60 ? 'warn' : ''}">CPU <b>${cpu}%</b></span>`,
    `<span class="chip">Effects delay <b>${fmtMs(hp.valid ? hp.fx : 0)}</b></span>`,
    `<span class="chip ${v.under ? 'warn' : ''}">Cable dropouts <b>${v.under || 0}</b></span>`,
    `<span class="chip ${v.over ? 'warn' : ''}">Cable overflows <b>${v.over || 0}</b></span>`
  ];
  if (s.bad) chips.push(`<span class="chip bad">Blocked bad audio <b>${s.bad}</b></span>`);
  $('#stats').innerHTML = chips.join('');
}

/* ------------------------------------------------------------------ devices */
function mkSelect(host, { items, value, placeholder, disabled, onPick }) {
  host.className = 'select'; host.innerHTML = '';
  const cur = items.find(i => i.value === value);
  const b = document.createElement('button');
  b.type = 'button'; b.disabled = !!disabled;
  b.className = cur ? '' : 'ph';
  b.innerHTML = `<span>${esc(cur ? cur.label : placeholder || 'Choose')}</span>${CHEV}`;
  const list = document.createElement('div'); list.className = 'list'; list.hidden = true;
  list.innerHTML = items.length
    ? items.map(i => `<button type="button" data-v="${esc(i.value)}" class="${i.value === value ? 'on' : ''}">${esc(i.label)}</button>`).join('')
    : '<div class="empty">Nothing found</div>';
  b.onclick = () => {
    const open = !host.classList.contains('open');
    $$('.select.open').forEach(s => s.classList.remove('open'));
    $$('.select .list').forEach(l => l.hidden = true);
    if (open) { host.classList.add('open'); list.hidden = false; }
  };
  list.onclick = e => {
    const t = e.target.closest('button'); if (!t) return;
    host.classList.remove('open'); list.hidden = true;
    if (t.dataset.v !== String(value)) onPick(items.find(i => String(i.value) === t.dataset.v).value);
  };
  host.append(b, list);
}

function mkBufs(host, sizes, cur, rate, onPick) {
  host.innerHTML = '';
  if (!sizes.length) { host.innerHTML = '<span class="none">No buffer sizes reported by the driver.</span>'; return; }
  for (const s of sizes) {
    const b = document.createElement('button');
    b.className = 'buf' + (s === cur ? ' on' : '');
    b.innerHTML = `<b>${s}</b><small>${rate ? fmtMs(s * 1000 / rate) : 'samples'}</small>`;
    b.onclick = () => { if (s !== cur) onPick(s); };
    host.appendChild(b);
  }
}

function onDevices(d) {
  S.dev = d;
  const f = d.iface, v = d.virt;

  mkSelect($('#selIface'), { items: f.list.map(n => ({ value: n, label: n })), value: f.current, placeholder: 'No interface found', onPick: n => send('iface', { name: n }) });
  mkSelect($('#selIn'),  { items: f.ins.map((n, i)  => ({ value: i, label: n })), value: f.inSel,  placeholder: 'No input',  disabled: !f.ins.length,  onPick: i => send('channels', { inCh: i, outPair: f.outSel }) });
  mkSelect($('#selOut'), { items: f.outs.map((n, i) => ({ value: i, label: n })), value: f.outSel, placeholder: 'No output', disabled: !f.outs.length, onPick: i => send('channels', { inCh: f.inSel, outPair: i }) });
  mkBufs($('#bufHp'), f.sizes, f.size, f.rate, s => send('hpBuffer', { size: s }));
  $('#btnPanel').disabled = !f.hasPanel;
  $('#hintHp').textContent = f.sizes.length > 1
    ? 'Set the headphone buffer here or in the driver control panel. Lower values give less delay.'
    : 'This driver fixes the buffer size. Change it in the interface control panel (Control Center).';

  const seg = $('#segVType'); seg.innerHTML = '<i class="thumb"></i>';
  v.types.forEach((t, i) => {
    const b = document.createElement('button'); b.textContent = t.replace('Windows Audio (Exclusive Mode)', 'Exclusive').replace('Windows Audio', 'Shared'); b.title = t;
    b.setAttribute('aria-selected', t === v.type);
    b.onclick = () => { if (t !== v.type) send('virtual', { type: t, device: '', size: 0 }); };
    seg.appendChild(b);
  });
  setSeg(seg, Math.max(0, v.types.indexOf(v.type)), Math.max(1, v.types.length));

  mkSelect($('#selVDev'), { items: v.list.map(n => ({ value: n, label: n })), value: v.current, placeholder: 'No output device', onPick: n => send('virtual', { type: v.type, device: n, size: 0 }) });
  mkBufs($('#bufV'), v.sizes, v.size, v.rate, s => send('virtual', { type: v.type, device: v.current, size: s }));
}

/* ------------------------------------------------------------------ plugins */
function onChain(chain) { S.chain = chain; renderChain(); }

const ICO = {
  grip: '<svg viewBox="0 0 24 24"><circle cx="9" cy="6" r="1.6"/><circle cx="15" cy="6" r="1.6"/><circle cx="9" cy="12" r="1.6"/><circle cx="15" cy="12" r="1.6"/><circle cx="9" cy="18" r="1.6"/><circle cx="15" cy="18" r="1.6"/></svg>',
  x: '<svg viewBox="0 0 24 24"><path d="M6 6l12 12M18 6L6 18"/></svg>'
};

function renderChain() {
  const host = $('#chain'); host.innerHTML = '';
  S.chain.forEach((s, i) => {
    const loaded = !!s.name && !s.loading;
    const row = document.createElement('div');
    row.className = 'slot' + (loaded ? '' : s.loading ? ' loading' : ' empty') + (s.bypassed ? ' byp' : '');
    row.dataset.uid = s.uid;
    row.innerHTML = `<span class="grip" title="Drag to reorder">${ICO.grip}</span><span class="idx">FX ${i + 1}</span>
      <button class="nm">${s.loading ? '<i class="spin"></i>Loading' : esc(s.name || 'Insert an effect')}</button>
      <button class="btn ui" ${loaded ? '' : 'disabled'}>Open</button>
      <button class="sw ${loaded && !s.bypassed ? 'on' : ''} ${loaded ? '' : 'dis'}" role="switch" aria-checked="${loaded && !s.bypassed}" aria-label="Enable effect"></button>
      <button class="x" aria-label="Remove slot">${ICO.x}</button>`;
    $('.nm', row).onclick = () => !s.loading && openPicker(s.uid);
    $('.ui', row).onclick = () => send('editor', { uid: s.uid });
    $('.sw', row).onclick = () => send('bypass', { uid: s.uid, v: !s.bypassed });
    $('.x', row).onclick  = () => send('removeSlot', { uid: s.uid });

    const grip = $('.grip', row);
    grip.onmousedown = () => row.draggable = true;
    row.ondragstart = e => { dragUid = s.uid; row.classList.add('drag'); e.dataTransfer.effectAllowed = 'move'; e.dataTransfer.setData('text/plain', String(s.uid)); };
    row.ondragend = () => { row.draggable = false; row.classList.remove('drag'); $$('.slot.over').forEach(r => r.classList.remove('over')); dragUid = null; };
    row.ondragover = e => { if (dragUid !== null && dragUid !== s.uid) { e.preventDefault(); row.classList.add('over'); } };
    row.ondragleave = () => row.classList.remove('over');
    row.ondrop = e => { e.preventDefault(); row.classList.remove('over'); if (dragUid !== null && dragUid !== s.uid) send('moveSlot', { uid: dragUid, index: i }); };
    host.appendChild(row);
  });
}
let dragUid = null;

function onPlugins(list) {
  S.plugins = list.map(p => Object.assign(p, { nl: p.n.toLowerCase(), ml: (p.m || '').toLowerCase(), cl: (p.c || '').toLowerCase(), k: p.n.toLowerCase().replace(/[^a-z0-9]/g, '') }));
  if (!$('#picker').hidden) refilter();
}

function onScan(s) {
  S.scan = s;
  const bar = $('#scanbar'), btn = $('#btnScan');
  btn.textContent = s.running ? 'Stop' : 'Scan';
  btn.classList.toggle('red', s.running); btn.classList.toggle('blue', !s.running);
  if (s.running) {
    const pct = s.total ? Math.round(s.done / s.total * 100) : 0;
    bar.innerHTML = `<div class="line"><span>${s.total ? `Scanning ${s.done} of ${s.total}` : 'Looking for plugins'}</span><span>${esc(s.current || '')}</span></div><div class="prog ${s.total ? '' : 'ind'}"><i style="width:${pct}%"></i></div>`;
  } else {
    bar.innerHTML = `<div class="line"><span><b style="color:var(--text)">${s.count}</b> plugins indexed${s.failed ? ` · ${s.failed} skipped` : ''}${s.cancelled ? ' · stopped' : ''}</span></div>`;
  }
}

function scanMenu(anchor) {
  const m = $('#menu');
  if (!m.hidden) { m.hidden = true; return; }
  const folders = S.scan.folders || [];
  m.innerHTML = `<button data-a="scan">Rescan changed plugins</button><button data-a="full">Full rescan</button><hr><button data-a="add">Add plugin folder…</button>` +
    (folders.length ? '<hr><small>Extra folders</small>' + folders.map((f, i) => `<button class="fold" data-a="rm" data-i="${i}"><span title="${esc(f)}">${esc(f)}</span><em>Remove</em></button>`).join('') : '');
  m.hidden = false;
  const r = anchor.getBoundingClientRect();
  m.style.top = (r.bottom + 8) + 'px';
  m.style.left = Math.max(10, Math.min(innerWidth - m.offsetWidth - 10, r.right - m.offsetWidth)) + 'px';
  m.onclick = e => {
    const b = e.target.closest('button'); if (!b) return;
    m.hidden = true;
    ({ scan: () => send('scan', { full: false }), full: () => send('scan', { full: true }), add: () => send('addFolder'),
       rm: () => send('removeFolder', { path: folders[+b.dataset.i] }) })[b.dataset.a]();
  };
}

/* ------------------------------------------------------------ plugin picker */
let pickUid = null, shown = [], selIdx = 0, filterMode = 0;

function initPicker() {
  const q = $('#q'), seg = $('#segFilter');
  $$('button', seg).forEach(b => b.onclick = () => { filterMode = +b.dataset.f; paintFilter(); refilter(); q.focus(); });
  paintFilter();
  q.oninput = () => refilter();
  $('#qClose').onclick = closePicker;
  $('#picker').addEventListener('pointerdown', e => { if (e.target.id === 'picker') closePicker(); });
  document.addEventListener('keydown', e => {
    if ($('#picker').hidden) return;
    if (e.key === 'Escape') { closePicker(); e.preventDefault(); }
    else if (e.key === 'ArrowDown') { moveSel(1); e.preventDefault(); }
    else if (e.key === 'ArrowUp') { moveSel(-1); e.preventDefault(); }
    else if (e.key === 'Enter') { choose(selIdx); e.preventDefault(); }
  });
}
function paintFilter() {
  const seg = $('#segFilter'); setSeg(seg, filterMode, 3);
  $$('button', seg).forEach((b, i) => b.setAttribute('aria-selected', i === filterMode));
}
function openPicker(uid) {
  if (!S.plugins.length) { toast('warn', S.scan.running ? 'Plugins are still being indexed. Try again in a moment.' : 'No VST3 plugins indexed yet. Press Scan, or add a plugin folder from the menu.'); return; }
  pickUid = uid; $('#q').value = ''; $('#picker').hidden = false; refilter(); setTimeout(() => $('#q').focus(), 30);
}
function closePicker() { $('#picker').hidden = true; pickUid = null; }

const isSub = (hay, n) => { let j = 0; for (let i = 0; i < hay.length && j < n.length; i++) if (hay[i] === n[j]) j++; return j === n.length; };
function scoreTok(p, t) {
  if (p.nl.startsWith(t)) return 100;
  let any = false;
  for (let from = 0; ;) {
    const i = p.nl.indexOf(t, from); if (i < 0) break;
    any = true; if (i === 0 || !/[a-z0-9]/.test(p.nl[i - 1])) return 80; from = i + 1;
  }
  if (any) return 55;
  const k = t.replace(/[^a-z0-9]/g, '');
  if (k && p.k.includes(k)) return 50;
  if (p.ml.startsWith(t)) return 45;
  if (p.ml.includes(t)) return 30;
  if (p.cl.includes(t)) return 20;
  if (t.length >= 2 && isSub(p.nl, t)) return 10;
  return 0;
}
function refilter() {
  const toks = $('#q').value.trim().toLowerCase().split(/\s+/).filter(Boolean);
  const out = [];
  for (const p of S.plugins) {
    if (filterMode === 0 && p.i) continue;
    if (filterMode === 1 && !p.i) continue;
    let sc = 0, ok = true;
    for (const t of toks) { const s = scoreTok(p, t); if (!s) { ok = false; break; } sc += s; }
    if (ok) out.push([sc + Math.min(30, (p.u || 0) * 3), p]);
  }
  out.sort((a, b) => b[0] - a[0] || a[1].nl.localeCompare(b[1].nl));
  shown = out.map(o => o[1]); selIdx = 0;
  $('#qCount').textContent = `${shown.length} of ${S.plugins.length} plugins`;
  renderList(toks);
}
function hl(text, toks) {
  let s = esc(text);
  for (const t of toks) { if (!t) continue; const re = new RegExp('(' + t.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')', 'ig'); s = s.replace(re, '<mark>$1</mark>'); }
  return s;
}
function renderList(toks) {
  const host = $('#plist'), LIM = 150;
  if (!shown.length) { host.innerHTML = '<div class="pempty">No plugin matches your search.</div>'; return; }
  host.innerHTML = shown.slice(0, LIM).map((p, i) => `<button class="pi ${i === selIdx ? 'sel' : ''}" data-i="${i}" role="option"><div class="m"><div class="n">${hl(p.n, toks)}</div><div class="d">${esc(p.m)}${p.c ? ' · ' + esc(p.c) : ''}${p.u ? ' · used ' + p.u + '×' : ''}</div></div><span class="f">${esc(p.f)}</span></button>`).join('')
    + (shown.length > LIM ? `<div class="pempty">Showing the best ${LIM}. Type to narrow the list.</div>` : '');
  host.scrollTop = 0;
  host.onclick = e => { const b = e.target.closest('.pi'); if (b) choose(+b.dataset.i); };
}
function moveSel(d) {
  if (!shown.length) return;
  const max = Math.min(shown.length, 150) - 1;
  const items = $$('.pi'); items[selIdx]?.classList.remove('sel');
  selIdx = clamp(selIdx + d, 0, max);
  const el = items[selIdx]; el?.classList.add('sel'); el?.scrollIntoView({ block: 'nearest' });
}
function choose(i) {
  const p = shown[i]; if (!p || pickUid === null) return;
  send('load', { uid: pickUid, id: p.id });
  closePicker();
}
})();
