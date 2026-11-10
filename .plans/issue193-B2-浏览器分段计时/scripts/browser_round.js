// Segmented browser access/recovery probe for #193 line B2 (upgrade of the
// e2b/joint-round browser_round.js). Measures user-visible phases SEPARATELY:
//   seg1 page load      : nav start -> appsite iframe ready (video element present)
//   seg2 ws open        : WS open  -> first WS message        (per connection cycle)
//   seg3 join           : first WS message -> first decodable frame
//                         (first vfc callback OR readyState>=2 with currentTime advancing)
//   seg4 steady playback: fps, stalls, teardown signatures, post-offline recovery
// Scenarios (parity with @604 campaign): late-join (default), disconnect/reconnect
// (--offline-at/--offline-for), dual client (--dual), sustained (--dur), tab close
// (--close-at). Read-only observation: no config/stop/clicks (no auto Retry clicks
// unless --auto-retry, which reproduces the old harness behavior for comparison).
// Usage: node browser_round.js --dur 120 --out browser.jsonl --tag late [--dual]
//                 [--offline-at 90 --offline-for 20] [--close-at 60] [--auto-retry]
// Env: BASE (default http://192.168.93.111), USER (admin), DEVICE_PASS, PW_EXEC.
const { chromium } = require('playwright');
const fs = require('fs');

const BASE = process.env.BASE || 'http://192.168.93.111';
const USER = process.env.USER_OVERRIDE || 'admin';
const PASS = process.env.DEVICE_PASS;
const args = process.argv.slice(2);
function arg(name, def) {
  const i = args.indexOf('--' + name);
  return i >= 0 ? args[i + 1] : def;
}
const DUR = parseFloat(arg('dur', '120'));
const OUT = arg('out', 'browser.jsonl');
const TAG = arg('tag', 'round');
const OFFLINE_AT = arg('offline-at') ? parseFloat(arg('offline-at')) : null;
const OFFLINE_FOR = arg('offline-for') ? parseFloat(arg('offline-for')) : 20;
const CLOSE_AT = arg('close-at') ? parseFloat(arg('close-at')) : null;
const DUAL = args.includes('--dual');
const AUTO_RETRY = args.includes('--auto-retry');
const ACQ_POLL_MS = 250;
const ACQ_TIMEOUT_S = 90;

const log = (obj) => fs.appendFileSync(OUT, JSON.stringify({ t_epoch: Date.now() / 1000, ...obj }) + '\n');

const FRAME_PROBE_SRC = `(() => {
  if (window.__segProbe) return true;
  const p = {
    installedAt: Date.now(),
    firstVfc: null,
    vfcCount: 0,
    lastVfcAt: null,
    decodableEvents: [],
    decodableArmed: true,
    readyDrops: [],
    prevReady: -1,
    prevCt: -1,
    readySamples: [],
    wv: [],
    vfcSamples: [],
    lastVfcSampleAt: Date.now(),
  };
  window.__segProbe = p;
  const v = document.querySelector('video');
  if (!v) return false;
  const markDecodable = (how) => {
    const now = Date.now();
    if (p.decodableArmed) {
      p.decodableEvents.push({ t: now, how });
      p.decodableArmed = false;
    }
    p.lastVfcAt = now;
  };
  if (v.requestVideoFrameCallback) {
    const cb = () => {
      p.vfcCount += 1;
      if (p.firstVfc === null) p.firstVfc = Date.now();
      markDecodable('vfc');
      v.requestVideoFrameCallback(cb);
    };
    v.requestVideoFrameCallback(cb);
  }
  setInterval(() => {
    const el = document.querySelector('video');
    if (!el) return;
    const ready = el.readyState;
    const w = el.videoWidth;
    const ct = el.currentTime;
    if (p.prevReady >= 2 && ready < 2) p.readyDrops.push({ t: Date.now(), from: p.prevReady, to: ready });
    p.prevReady = ready;
    if (!v.requestVideoFrameCallback || p.firstVfc === null) {
      if (ready >= 2 && w > 0 && p.prevCt >= 0 && ct > p.prevCt) markDecodable('readyState');
    }
    p.prevCt = ct;
    p.readySamples.push({ t: Date.now(), ready, w, ct, paused: el.paused, vfc: p.vfcCount });
    if (p.readySamples.length > 4000) p.readySamples.splice(0, 2000);
    const now = Date.now();
    if (now - p.lastVfcSampleAt >= 1000) {
      p.vfcSamples.push({ t: now, count: p.vfcCount });
      p.lastVfcSampleAt = now;
      if (p.vfcSamples.length > 7200) p.vfcSamples.splice(0, 3600);
    }
    if (p.lastVfcAt !== null && now - p.lastVfcAt > 1000) p.decodableArmed = true;
  }, 200);
  ['wv_open', 'wv_close', 'wv_error', 'wv_work'].forEach((name) => {
    window.addEventListener(name, (e) => {
      p.wv.push({ t: Date.now(), name, detail: e.detail === undefined ? null : String(e.detail).slice(0, 80) });
      if (p.wv.length > 500) p.wv.shift();
    });
  });
  window.__segProbeRead = () => ({
    installedAt: p.installedAt,
    firstVfc: p.firstVfc,
    vfcCount: p.vfcCount,
    lastVfcAt: p.lastVfcAt,
    decodableEvents: p.decodableEvents.slice(-40),
    readyDrops: p.readyDrops.slice(-40),
    readyTail: p.readySamples.slice(-6),
    wv: p.wv.slice(-60),
    vfcTail: p.vfcSamples.slice(-10),
  });
  return true;
})()`;

async function loginAndNav(p, label) {
  const marks = {};
  marks.nav_start = Date.now() / 1000;
  log({ kind: 'mark', client: label, mark: 'nav_start', t_in_win: null });
  await p.goto(BASE + '/', { waitUntil: 'domcontentloaded', timeout: 30000 });
  await p.waitForTimeout(4000);
  const hasLogin = await p.evaluate(() => !!document.querySelector('#username'));
  if (hasLogin) {
    await p.evaluate(([u, pw]) => {
      const setter = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value').set;
      const un = document.querySelector('#username'); un.disabled = false;
      setter.call(un, u); un.dispatchEvent(new Event('input', { bubbles: true }));
      const pd = document.querySelector('input[type=password]'); pd.disabled = false;
      setter.call(pd, pw); pd.dispatchEvent(new Event('input', { bubbles: true }));
    }, [USER, PASS]);
    await p.locator('button', { hasText: 'login' }).click({ timeout: 15000 });
    await p.waitForTimeout(3000);
  }
  marks.login_done = Date.now() / 1000;
  log({ kind: 'mark', client: label, mark: 'login_done' });
  await p.goto(BASE + '/application-management', { waitUntil: 'domcontentloaded', timeout: 30000 });
  marks.app_tab = Date.now() / 1000;
  log({ kind: 'mark', client: label, mark: 'app_tab' });
  return marks;
}

async function waitAppsite(p, label) {
  let frame = null;
  for (let i = 0; i < 60; i++) {
    frame = p.frames().find((f) => f.url().includes('appsite'));
    if (frame) break;
    await p.waitForTimeout(500);
  }
  if (!frame) throw new Error('appsite frame not found');
  log({ kind: 'mark', client: label, mark: 'appsite_frame_attached', url: frame.url() });
  for (let i = 0; i < 60; i++) {
    if ((await frame.locator('video').count()) > 0) break;
    await p.waitForTimeout(500);
  }
  const hasVideo = (await frame.locator('video').count()) > 0;
  if (!hasVideo) throw new Error('appsite frame has no video element');
  log({ kind: 'mark', client: label, mark: 'appsite_video_present' });
  return frame;
}

function attachWsProbe(page, label) {
  const ws = { cycles: [], seen: {} };
  page.on('websocket', (s) => {
    const url = s.url();
    const id = label + ':' + url + ':' + (ws.cycles.length + 1);
    const cycle = { id, client: label, url, created: Date.now() / 1000, open: null, firstMsg: null, msgs: 0, close: null, error: null };
    ws.cycles.push(cycle);
    ws.seen[id] = cycle;
    log({ kind: 'ws', client: label, event: 'created', url });
    const isOpen = s.on && typeof s.on === 'function';
    if (isOpen) s.on('open', () => {
      cycle.open = Date.now() / 1000;
      log({ kind: 'ws', client: label, event: 'open', url });
    });
    s.on('framereceived', () => {
      cycle.msgs += 1;
      if (cycle.firstMsg === null) {
        cycle.firstMsg = Date.now() / 1000;
        log({ kind: 'ws', client: label, event: 'first-msg', url });
      }
    });
    s.on('socketerror', () => {
      cycle.error = Date.now() / 1000;
      log({ kind: 'ws', client: label, event: 'socketerror', url });
    });
    s.on('close', () => {
      cycle.close = Date.now() / 1000;
      log({ kind: 'ws', client: label, event: 'close', url });
    });
  });
  return ws;
}

async function readProbe(frame, label) {
  try {
    return await frame.evaluate((l) => {
      if (!window.__segProbeRead) return null;
      const s = window.__segProbeRead();
      return { client: l, ...s };
    }, label);
  } catch (e) {
    return { client: label, err: String(e).split('\n')[0] };
  }
}

(async () => {
  const browser = await chromium.launch({
    headless: true,
    executablePath: process.env.PW_EXEC || undefined,
    args: ['--disable-background-timer-throttling', '--disable-backgrounding-occluded-windows',
      '--disable-renderer-backgrounding', '--autoplay-policy=no-user-gesture-required'],
  });
  const ctx = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const t0 = Date.now();
  log({ kind: 'meta', start_wall: t0 / 1000, dur: DUR, tag: TAG, dual: DUAL, offline_at: OFFLINE_AT, offline_for: OFFLINE_FOR, close_at: CLOSE_AT, auto_retry: AUTO_RETRY, probe: 'segments-v2' });

  const clients = [];
  const mkClient = async (label) => {
    const page = await ctx.newPage();
    const marks = await loginAndNav(page, label);
    const wsProbe = attachWsProbe(page, label);
    const frame = await waitAppsite(page, label);
    try {
      await frame.evaluate(FRAME_PROBE_SRC);
      log({ kind: 'mark', client: label, mark: 'frame_probe_installed' });
    } catch (e) {
      log({ kind: 'event', event: 'frame_probe_failed', client: label, err: String(e).split('\n')[0] });
    }
    log({ kind: 'mark', client: label, mark: 'host_player_open' });
    return { label, page, frame, marks, wsProbe, firstDecodable: null, prevVfc: 0, prevT: Date.now(), lastRetry: Date.now() };
  };

  const c1 = await mkClient('c1');
  clients.push(c1);
  let c2 = null;
  if (DUAL) {
    c2 = await mkClient('c2');
    clients.push(c2);
  }

  const winStart = Date.now();
  let offlineSet = false;
  let offlineCleared = false;
  let closed = false;
  let acqDone = false;

  while (Date.now() - winStart < DUR * 1000) {
    const tIn = (Date.now() - winStart) / 1000;
    for (const cl of clients) {
      const rf = cl.page.frames().find((f) => f.url().includes('appsite'));
      if (rf && rf !== cl.frame) {
        cl.frame = rf;
        try { await rf.evaluate(FRAME_PROBE_SRC); } catch (e) { }
        log({ kind: 'mark', client: cl.label, mark: 'appsite_frame_reattached', t_in_win: tIn });
      }
    }
    if (OFFLINE_AT !== null && !offlineSet && tIn >= OFFLINE_AT) {
      offlineSet = true;
      await ctx.setOffline(true);
      log({ kind: 'event', event: 'offline_set', t_in_win: tIn });
    }
    if (OFFLINE_AT !== null && offlineSet && !offlineCleared && tIn >= OFFLINE_AT + OFFLINE_FOR) {
      offlineCleared = true;
      await ctx.setOffline(false);
      log({ kind: 'event', event: 'offline_cleared', t_in_win: tIn });
    }
    if (CLOSE_AT !== null && !closed && tIn >= CLOSE_AT) {
      closed = true;
      for (const cl of clients) await cl.page.close();
      log({ kind: 'event', event: 'sudden_tab_close', t_in_win: tIn });
      break;
    }
    if (AUTO_RETRY && tIn >= 30 && Date.now() - c1.lastRetry > 25000) {
      try {
        const v = await c1.frame.evaluate(() => { const e = document.querySelector('video'); return e ? e.readyState : -1; });
        if (v < 2) {
          await c1.frame.getByText('Retry video', { exact: false }).first().click({ timeout: 3000 });
          log({ kind: 'event', event: 'retry_video_clicked', t_in_win: tIn });
        }
      } catch (e) {
        log({ kind: 'event', event: 'retry_video_unavailable', err: e.message.split('\n')[0], t_in_win: tIn });
      }
      c1.lastRetry = Date.now();
    }
    try {
      let allDecodable = true;
      for (const cl of clients) {
        const s = await readProbe(cl.frame, cl.label);
        if (!s || s.err || s.installedAt === undefined) {
          log({ kind: 'sample_error', client: cl.label, t_in_win: tIn, err: s && s.err });
          allDecodable = false;
          continue;
        }
        if (cl.firstDecodable === null && s.decodableEvents.length > 0) {
          cl.firstDecodable = s.decodableEvents[0].t / 1000;
          log({ kind: 'mark', client: cl.label, mark: 'first_decodable', how: s.decodableEvents[0].how, t_in_win: tIn });
        }
        if (cl.firstDecodable === null) allDecodable = false;
        const tail = s.readyTail && s.readyTail.length ? s.readyTail[s.readyTail.length - 1] : null;
        const now = Date.now();
        const fps = (s.vfcCount - cl.prevVfc) / ((now - cl.prevT) / 1000);
        log({
          kind: 'sample', client: cl.label, t_in_win: Math.round(tIn * 10) / 10,
          video: tail ? { ready: tail.ready, w: tail.w, ct: tail.ct, paused: tail.paused } : null,
          vfc: { count: s.vfcCount, last_at: s.lastVfcAt },
          vfc_fps: Math.round(fps * 100) / 100,
          decodable_events: s.decodableEvents.length,
          ready_drops: s.readyDrops.length,
        });
        cl.prevVfc = s.vfcCount;
        cl.prevT = now;
      }
      if (!acqDone && allDecodable) {
        acqDone = true;
        log({ kind: 'mark', mark: 'acquisition_complete', t_in_win: tIn });
      }
    } catch (e) {
      log({ kind: 'sample_error', error: e.message.split('\n')[0], t_in_win: tIn });
    }
    await clients[0].page.waitForTimeout(acqDone ? 1000 : ACQ_POLL_MS);
    if (!acqDone && tIn > ACQ_TIMEOUT_S) {
      log({ kind: 'event', event: 'acquisition_timeout', t_in_win: tIn });
      acqDone = true;
    }
  }

  for (const cl of clients) {
    const s = await readProbe(cl.frame, cl.label);
    if (s && s.installedAt !== undefined) {
      log({ kind: 'probe_final', client: cl.label, probe: { firstVfc: s.firstVfc, vfcCount: s.vfcCount, decodableEvents: s.decodableEvents, readyDrops: s.readyDrops, wv: s.wv, vfcTail: s.vfcTail } });
    }
  }
  log({ kind: 'end', tag: TAG });
  try { await ctx.close(); } catch (e) { }
  await browser.close();
  process.exit(0);
})().catch((e) => { log({ kind: 'fatal', error: e.message }); console.error('FATAL', e.message); process.exit(1); });
