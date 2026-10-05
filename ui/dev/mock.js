// Browser-only stand-in for the JUCE backend: open ui/index.html in Chrome/Edge to design the UI without the C++ app.
(() => {
  const L = {};
  const emit = (id, o) => (L[id] || []).forEach(f => f(JSON.parse(JSON.stringify(o))));
  const names = ['FabFilter Pro-Q 3','FabFilter Pro-C 2','FabFilter Pro-L 2','FabFilter Pro-R','Waves CLA-2A','Waves SSL E-Channel','Waves Renaissance Vox','iZotope Ozone 11','iZotope Nectar 4','Valhalla VintageVerb','Valhalla Supermassive','Soundtoys Decapitator','Soundtoys EchoBoy','Native Instruments Guitar Rig 6','TDR Nova','TDR Kotelnikov','Kilohearts Phase Plant','Serum','Vital','Omnisphere','Kontakt 7','Melodyne 5','Auto-Tune Pro X','Oeksound Soothe2','Slate Digital FG-X','Eventide H3000','Softube Tube-Tech CL 1B','Black Box HG-2','Acon Digital DeVerberate','Youlean Loudness Meter 2'];
  const mfr = n => n.split(' ')[0];
  const plugins = names.map((n, i) => ({ id: 'vst3:' + i, n, m: mfr(n), c: i % 7 === 0 ? 'Fx|EQ' : i % 5 === 0 ? 'Fx|Dynamics' : 'Fx', f: 'VST3', i: ['Serum','Vital','Omnisphere','Kontakt 7','Kilohearts Phase Plant'].includes(n), u: i % 4 }));
  const st = {
    settings: { mic: true, mon: true, fx: true, gainIn: 3.5, gainMon: -6, gainVirt: 0 },
    dev: {
      iface: { list: ['Arturia MiniFuse ASIO Driver', 'Realtek ASIO', 'ASIO4ALL v2'], current: 'Arturia MiniFuse ASIO Driver', type: 'ASIO', ins: ['Input 1', 'Input 2'], inSel: 0,
               outs: ['Output 1 / Output 2', 'Output 3 / Output 4'], outSel: 0, sizes: [32, 64, 128, 256, 512, 1024], size: 64, rate: 48000, hasPanel: true },
      virt: { types: ['Windows Audio', 'DirectSound', 'Windows Audio (Exclusive Mode)'], type: 'Windows Audio', list: ['CABLE Input (VB-Audio Virtual Cable)', 'Speakers (Realtek)', 'Headphones (Arturia)'], current: 'CABLE Input (VB-Audio Virtual Cable)', sizes: [128, 256, 480, 512, 960, 1024], size: 480, rate: 48000, running: true }
    },
    chain: [{ uid: 1, name: 'FabFilter Pro-Q 3', bypassed: false, loading: false }, { uid: 2, name: 'Waves CLA-2A', bypassed: true, loading: false }, { uid: 3, name: '', bypassed: false, loading: false }],
    scan: { running: false, count: plugins.length, failed: 2, cancelled: false, folders: ['E:\\Installed Program'], done: 0, total: 0, current: '' }
  };
  let nextUid = 4;
  const sendChain = () => emit('chain', st.chain);
  const stats = () => {
    const f = st.dev.iface, r = f.rate, hpIn = f.size * 1000 / r, hpOut = f.size * 1000 / r;
    const fx = st.chain.filter(c => c.name && !c.bypassed).length * 0.0;
    const v = st.dev.virt, dev = Math.max(v.size, 480) * 1000 / v.rate;
    emit('stats', { ok: true, status: `${f.type} · ${(r / 1000).toFixed(1)} kHz · ${f.size} spl`, type: f.type, cpu: 0.18 + Math.random() * 0.04, bad: 0,
      hp: { valid: true, est: false, in: hpIn + 0.3, out: hpOut + 0.4, fx, total: hpIn + hpOut + 0.7 + fx, buf: f.size, rate: r },
      v: { valid: true, in: hpIn + 0.3, fx, queue: 21.3 + Math.random(), dev, total: hpIn + 0.3 + fx + 21.8 + dev, buf: v.size, rate: v.rate, under: 0, over: 0 } });
  };
  const all = () => { emit('settings', st.settings); emit('devices', st.dev); sendChain(); emit('plugins', plugins); emit('scan', st.scan); stats(); };
  const cmds = {
    ready: all,
    switch: d => st.settings[d.k] = d.v,
    gain: d => st.settings['gain' + { in: 'In', mon: 'Mon', virt: 'Virt' }[d.k]] = d.db,
    iface: d => { st.dev.iface.current = d.name; emit('devices', st.dev); },
    channels: d => { st.dev.iface.inSel = d.inCh; st.dev.iface.outSel = d.outPair; emit('devices', st.dev); },
    hpBuffer: d => { st.dev.iface.size = d.size; emit('devices', st.dev); stats(); },
    virtual: d => { if (d.type) st.dev.virt.type = d.type; if (d.device) st.dev.virt.current = d.device; if (d.size) st.dev.virt.size = d.size; emit('devices', st.dev); stats(); },
    panel: () => emit('toast', { kind: 'ok', text: 'Mock: control panel opened.' }),
    addSlot: () => { st.chain.push({ uid: nextUid++, name: '', bypassed: false, loading: false }); sendChain(); },
    removeSlot: d => { st.chain = st.chain.filter(c => c.uid !== d.uid); sendChain(); },
    bypass: d => { st.chain.find(c => c.uid === d.uid).bypassed = d.v; sendChain(); },
    moveSlot: d => { const i = st.chain.findIndex(c => c.uid === d.uid); const [s] = st.chain.splice(i, 1); st.chain.splice(d.index, 0, s); sendChain(); },
    load: d => { const s = st.chain.find(c => c.uid === d.uid); s.loading = true; sendChain(); setTimeout(() => { s.loading = false; s.name = plugins.find(p => p.id === d.id).n; sendChain(); }, 700); },
    scan: () => { const s = st.scan; s.running = true; s.total = 40; s.done = 0; const t = setInterval(() => { s.done++; s.current = 'Plugin_' + s.done + '.vst3'; emit('scan', s); if (s.done >= s.total) { clearInterval(t); s.running = false; emit('scan', s); } }, 90); emit('scan', s); },
    scanStop: () => { st.scan.running = false; st.scan.cancelled = true; emit('scan', st.scan); }
  };
  window.__JUCE__ = { backend: {
    addEventListener: (id, fn) => { (L[id] = L[id] || []).push(fn); },
    emitEvent: (id, o) => { if (id === 'cmd' && cmds[o.c]) cmds[o.c](o); }
  } };
  setInterval(stats, 250);
  let ph = 0;
  setInterval(() => { ph += .2; const w = a => Math.max(0, .35 + .3 * Math.sin(ph * a) + (Math.random() - .5) * .25);
    emit('meters', { i: w(1) * .5, m: w(1.1) * .45, v: w(.9) * .5 }); }, 33);
})();
