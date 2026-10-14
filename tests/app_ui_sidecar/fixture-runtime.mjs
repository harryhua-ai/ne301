import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdirSync, readFileSync, realpathSync } from 'node:fs'
import http from 'node:http'
import path from 'node:path'
import { tmpdir } from 'node:os'
import { chromium } from 'playwright-core'
import { fileURLToPath } from 'node:url'

const fixtureRoot = path.dirname(fileURLToPath(import.meta.url))
const evidenceRoot = path.join(fixtureRoot, 'evidence')
const metrics = JSON.parse(readFileSync(path.join(evidenceRoot, 'fixture-metrics.json'), 'utf8'))
const tempRoot = path.join(realpathSync(tmpdir()), `ne301-app-ui-sidecar-${createHash('sha256').update(fixtureRoot).digest('hex').slice(0, 12)}`)
const distRoot = path.join(tempRoot, 'dist')
const mimeTypes = {
  '.css': 'text/css; charset=utf-8',
  '.html': 'text/html; charset=utf-8',
  '.ico': 'image/x-icon',
  '.js': 'text/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2'
}

const hostScript = `
const frame = document.querySelector('#sidecar-frame')
const hostOrigin = location.origin
const state = {
  expectedOrigin: frame.dataset.origin,
  nonce: '',
  generation: 0,
  accepted: [],
  rejected: [],
  ready: false,
  config: {
    enable: true,
    line_x1_permille: 120,
    line_y1_permille: 520,
    line_x2_permille: 850,
    line_y2_permille: 480,
    outside_x_permille: 300,
    outside_y_permille: 800,
    conf_threshold_permille: 300,
    max_dist_permille: 180,
    target_class_name: 'person',
    model_name: 'yolo11n',
    model_pp_type: 'tracking',
    track_history_k: 8,
    max_miss: 20,
    k_confirm: 3,
    window_minutes: 5,
    mqtt_report_enable: false,
    webhook_report_enable: false,
    tracks_report_enable: false,
    heat_grid_enable: false,
    backlog_capacity: 100
  },
  stats: {
    window_in: 4,
    window_out: 2,
    window_start_ts: 1730000000,
    total_in: 40,
    total_out: 37,
    boot_id: 1,
    boot_id_kind: 'fixture',
    last_report_ts: 1730000000,
    dropped_windows_mqtt: 0,
    dropped_windows_webhook: 0,
    heat_grid: []
  },
  backlog: { mqtt: { count: 0 }, webhook: { count: 0 } },
  videoOpen: false
}
const reject = (reason, message) => {
  state.rejected.push({ reason, requestId: message?.requestId || message?.id || null })
}
const nonceValue = () => {
  const bytes = crypto.getRandomValues(new Uint8Array(24))
  return Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('')
}
const sendBootstrap = () => {
  state.nonce = nonceValue()
  state.generation += 1
  state.ready = false
  frame.contentWindow.postMessage({ type: 'bootstrap', nonce: state.nonce }, state.expectedOrigin)
}
const validConfigPatch = payload => {
  if (!payload || typeof payload !== 'object' || Array.isArray(payload)) return false
  const ranges = {
    line_x1_permille: [0, 1000],
    line_y1_permille: [0, 1000],
    line_x2_permille: [0, 1000],
    line_y2_permille: [0, 1000],
    outside_x_permille: [0, 1000],
    outside_y_permille: [0, 1000],
    conf_threshold_permille: [0, 1000],
    max_dist_permille: [0, 1000],
    track_history_k: [1, 128],
    max_miss: [0, 1000],
    k_confirm: [1, 100],
    window_minutes: [1, 1440],
    backlog_capacity: [0, 100000]
  }
  const booleans = new Set(['enable', 'mqtt_report_enable', 'webhook_report_enable', 'tracks_report_enable', 'heat_grid_enable'])
  const strings = new Set(['target_class_name', 'model_name', 'model_pp_type'])
  const allowed = new Set([...Object.keys(ranges), ...booleans, ...strings])
  return Object.entries(payload).every(([key, value]) => {
    if (!allowed.has(key)) return false
    if (booleans.has(key)) return typeof value === 'boolean'
    if (strings.has(key)) return typeof value === 'string' && value.length <= 128
    const [min, max] = ranges[key]
    return Number.isInteger(value) && value >= min && value <= max
  })
}
const handleRequest = message => {
  const payload = message.payload || {}
  let result
  if (message.capability === 'peopleCounting.config.read') result = structuredClone(state.config)
  else if (message.capability === 'peopleCounting.stats.read') result = structuredClone(state.stats)
  else if (message.capability === 'peopleCounting.backlog.read') result = structuredClone(state.backlog)
  else if (message.capability === 'peopleCounting.config.write') {
    if (!validConfigPatch(payload)) return { error: 'payload' }
    state.config = { ...state.config, ...payload }
    result = { ok: true }
  } else if (message.capability === 'peopleCounting.stats.reset') {
    state.stats.total_in = 0
    state.stats.total_out = 0
    result = { ok: true }
  } else if (message.capability === 'peopleCounting.video.open') {
    state.videoOpen = true
    result = { ok: true }
  } else if (message.capability === 'peopleCounting.video.close') {
    state.videoOpen = false
    result = { ok: true }
  } else return { error: 'capability' }
  return { result }
}
window.addEventListener('message', event => {
  const message = event.data
  if (event.source !== frame.contentWindow) return reject('source', message)
  if (event.origin !== state.expectedOrigin) return reject('origin', message)
  if (!message || typeof message !== 'object') return reject('message', message)
  if (message.type === 'ready') {
    if (message.nonce !== state.nonce) return reject('nonce', message)
    state.ready = true
    return
  }
  if (message.type !== 'request') return reject('message', message)
  if (message.nonce !== state.nonce) return reject('nonce', message)
  const outcome = handleRequest(message)
  if (outcome.error) {
    reject(outcome.error, message)
    event.source.postMessage({ type: 'response', requestId: message.requestId, nonce: state.nonce, ok: false, error: { code: outcome.error === 'capability' ? 'CAPABILITY_DENIED' : 'CAPABILITY_PAYLOAD_INVALID' } }, event.origin)
    return
  }
  state.accepted.push({ capability: message.capability, requestId: message.requestId })
  event.source.postMessage({ type: 'response', requestId: message.requestId, nonce: state.nonce, ok: true, result: outcome.result }, event.origin)
})
frame.addEventListener('load', sendBootstrap)
frame.src = state.expectedOrigin + '/'
window.__hostAudit = state
window.__hostTest = {
  setExpectedOrigin(origin) { state.expectedOrigin = origin },
  switchSidecar(origin, version) {
    state.expectedOrigin = origin
    state.version = version
    state.nonce = ''
    state.ready = false
    frame.src = origin + '/'
  }
}
window.__hostDebug = {
  audit() { return structuredClone(state) },
  hostOrigin,
  sidecarOrigin: () => state.expectedOrigin
}
`

const serviceWorker = `
const cacheName = 'sidecar-probe-v1'
self.addEventListener('install', event => {
  event.waitUntil((async () => {
    const cache = await caches.open(cacheName)
    for (const path of ['/', '/build-label']) {
      const response = await fetch(path, { cache: 'no-store' })
      await cache.put(path, response)
    }
    await self.skipWaiting()
  })())
})
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()))
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url)
  if (url.origin !== self.location.origin || !['/', '/build-label'].includes(url.pathname)) return
  event.respondWith((async () => {
    const cache = await caches.open(cacheName)
    return await cache.match(url.pathname) || fetch(event.request)
  })())
})
`

const siblingPage = `<!doctype html><meta charset="utf-8"><script src="/sibling.js"></script>`
const siblingScript = `window.addEventListener('message', event => { if (event.source !== parent || !event.data?.hostOrigin) return; parent.postMessage(event.data.message, event.data.hostOrigin) })`
const hostPage = `<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Offline sidecar host</title><style>html,body{margin:0;width:100%;height:100%;overflow:hidden}#sidecar-frame{position:fixed;inset:0;width:100%;height:100%;border:0}#host-secret{position:fixed;left:-10000px}</style></head><body><main id="host-secret">Host credentials and DOM fixture</main><iframe id="sidecar-frame" name="sidecar-frame" sandbox="allow-scripts allow-same-origin" data-origin="__SIDECAR_ORIGIN__"></iframe><script src="/host.js"></script></body></html>`
const registerPage = '<!doctype html><meta charset="utf-8"><title>service worker setup</title><main>register</main>'
const attackerPage = origin => `<!doctype html><meta charset="utf-8"><title>third party embed</title><iframe src="${origin}/"></iframe>`

const listen = server => new Promise((resolve, reject) => {
  server.once('error', reject)
  server.listen(0, '127.0.0.1', () => {
    server.removeListener('error', reject)
    resolve(server.address().port)
  })
})

const send = (response, status, contentType, body, headers = {}) => {
  response.writeHead(status, { 'Content-Type': contentType, 'Cache-Control': 'no-store', ...headers })
  response.end(body)
}

const makeHostServer = state => http.createServer((request, response) => {
  const url = new URL(request.url, `http://${request.headers.host}`)
  response.setHeader('Set-Cookie', 'host_secret=host-only-test-secret; HttpOnly; SameSite=Strict; Path=/')
  if (url.pathname === '/') {
    const csp = `default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; frame-src ${state.sidecarOrigins.join(' ')}; connect-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'none'`
    return send(response, 200, 'text/html; charset=utf-8', hostPage.replace('__SIDECAR_ORIGIN__', state.sidecarOrigins[0]), { 'Content-Security-Policy': csp })
  }
  if (url.pathname === '/host.js') return send(response, 200, 'text/javascript; charset=utf-8', hostScript)
  if (url.pathname === '/register') return send(response, 200, 'text/html; charset=utf-8', registerPage)
  return send(response, 404, 'text/plain; charset=utf-8', 'not found')
})

const makeSidecarServer = state => {
  const server = http.createServer((request, response) => {
    const url = new URL(request.url, `http://${request.headers.host}`)
    const csp = `default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; connect-src 'self' ws://${url.host}; worker-src 'self' blob:; frame-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'; frame-ancestors ${state.hostOrigin}`
    response.setHeader('Content-Security-Policy', csp)
    if (url.pathname === '/build-label') return send(response, 200, 'application/json; charset=utf-8', JSON.stringify({ label: state.version }), { 'Content-Security-Policy': csp })
    if (url.pathname === '/sw.js') return send(response, 200, 'text/javascript; charset=utf-8', serviceWorker, { 'Service-Worker-Allowed': '/', 'Content-Security-Policy': "default-src 'self'; script-src 'self'; connect-src 'self'; object-src 'none'" })
    if (url.pathname === '/register') return send(response, 200, 'text/html; charset=utf-8', registerPage, { 'Content-Security-Policy': csp })
    if (url.pathname === '/sibling.html') return send(response, 200, 'text/html; charset=utf-8', siblingPage, { 'Content-Security-Policy': csp })
    if (url.pathname === '/sibling.js') return send(response, 200, 'text/javascript; charset=utf-8', siblingScript, { 'Content-Security-Policy': csp })
    if (url.pathname === '/embed.html') return send(response, 200, 'text/html; charset=utf-8', attackerPage(state.oldOrigin), { 'Content-Security-Policy': `default-src 'self'; script-src 'self'; frame-src ${state.oldOrigin}; object-src 'none'; base-uri 'none'` })
    const relative = decodeURIComponent(url.pathname).replace(/^\/+/, '')
    const filePath = path.resolve(distRoot, relative || 'index.html')
    if (!filePath.startsWith(`${distRoot}${path.sep}`) && filePath !== path.join(distRoot, 'index.html')) return send(response, 400, 'text/plain; charset=utf-8', 'invalid path', { 'Content-Security-Policy': csp })
    try {
      let body = readFileSync(filePath)
      if (relative === '' || relative === 'index.html') body = Buffer.from(body.toString().replace('__BUILD_ID__', state.version))
      const type = mimeTypes[path.extname(filePath)] || 'application/octet-stream'
      return send(response, 200, type, body, { 'Content-Security-Policy': csp })
    } catch {
      return send(response, 404, 'text/plain; charset=utf-8', 'not found', { 'Content-Security-Policy': csp })
    }
  })
  server.on('upgrade', (request, socket) => {
    const key = request.headers['sec-websocket-key']
    if (request.url !== '/stream' || !key) return socket.destroy()
    const accept = createHash('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64')
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`)
    state.webSockets.add(socket)
    socket.on('close', () => state.webSockets.delete(socket))
    socket.on('error', () => state.webSockets.delete(socket))
  })
  return server
}

const makeAttackerServer = state => http.createServer((request, response) => {
  const url = new URL(request.url, `http://${request.headers.host}`)
  if (url.pathname === '/embed.html') return send(response, 200, 'text/html; charset=utf-8', attackerPage(state.oldOrigin), { 'Content-Security-Policy': `default-src 'self'; frame-src ${state.oldOrigin}; object-src 'none'; base-uri 'none'` })
  return send(response, 200, 'text/html; charset=utf-8', '<!doctype html><title>attacker origin</title>')
})

const closeServer = server => new Promise(resolve => server.close(() => resolve()))

export async function startFixture() {
  assert.ok(distRoot && metrics.outputFileCount > 2)
  mkdirSync(evidenceRoot, { recursive: true })
  const state = { hostOrigin: '', sidecarOrigins: [], oldOrigin: '', version: 'v1', webSockets: new Set() }
  const hostServer = makeHostServer(state)
  const hostPort = await listen(hostServer)
  state.hostOrigin = `http://127.0.0.1:${hostPort}`
  const oldState = { ...state, version: 'v1', webSockets: new Set() }
  const newState = { ...state, version: 'v2', webSockets: new Set() }
  const oldServer = makeSidecarServer(oldState)
  const oldPort = await listen(oldServer)
  const newServer = makeSidecarServer(newState)
  const newPort = await listen(newServer)
  const attackerState = { ...state, oldOrigin: `http://127.0.0.1:${oldPort}` }
  const attackerServer = makeAttackerServer(attackerState)
  const attackerPort = await listen(attackerServer)
  state.sidecarOrigins = [`http://127.0.0.1:${oldPort}`, `http://127.0.0.1:${newPort}`]
  state.oldOrigin = state.sidecarOrigins[0]
  oldState.hostOrigin = state.hostOrigin
  oldState.oldOrigin = state.oldOrigin
  newState.hostOrigin = state.hostOrigin
  newState.oldOrigin = state.oldOrigin
  attackerState.oldOrigin = state.oldOrigin
  const browser = await chromium.launch({ channel: 'chrome', headless: false, args: ['--disable-background-networking', '--disable-component-update', '--disable-default-apps', '--no-first-run'] })
  const context = await browser.newContext({ viewport: { width: 1440, height: 1100 }, serviceWorkers: 'allow' })
  const page = await context.newPage()
  const observedHeaders = {}
  page.on('response', async response => {
    try {
      const responseUrl = new URL(response.url())
      if (responseUrl.pathname === '/') {
        observedHeaders[responseUrl.origin] = {
          contentSecurityPolicy: await response.headerValue('content-security-policy'),
          cacheControl: await response.headerValue('cache-control')
        }
      }
    } catch {}
  })
  const cachePage = await context.newPage()
  await cachePage.goto(`${state.oldOrigin}/register`)
  await cachePage.evaluate(async () => {
    await navigator.serviceWorker.register('/sw.js')
    await navigator.serviceWorker.ready
    if (!navigator.serviceWorker.controller) await new Promise(resolve => navigator.serviceWorker.addEventListener('controllerchange', resolve, { once: true }))
  })
  oldState.version = 'v2'
  const sameOriginLabel = await cachePage.evaluate(async () => (await (await fetch('/build-label', { cache: 'no-store' })).json()).label)
  const newOriginLabel = (await (await fetch(`${state.sidecarOrigins[1]}/build-label`, { cache: 'no-store' })).json()).label
  const cacheProbe = { sameOriginLabel, newOriginLabel }
  await cachePage.close()
  await page.goto(state.hostOrigin)
  await page.frame({ name: 'sidecar-frame' }).waitForFunction(() => Boolean(window.__fixtureBuildInfo?.htmlBuild), null, { timeout: 15000 }).catch(() => undefined)
  await page.frame({ name: 'sidecar-frame' }).waitForFunction(() => document.querySelector('#root')?.children.length > 0, null, { timeout: 15000 })
  const captureEvidence = async () => {
    await page.screenshot({ path: path.join(evidenceRoot, 'people-counting-sidecar.png'), fullPage: true })
    const audit = await page.evaluate(() => window.__hostDebug.audit())
    const sidecarMemory = await page.frame({ name: 'sidecar-frame' }).evaluate(() => {
      const memory = performance.memory
      return memory ? { usedJsHeapBytes: memory.usedJSHeapSize, totalJsHeapBytes: memory.totalJSHeapSize, jsHeapLimitBytes: memory.jsHeapSizeLimit } : { available: false }
    })
    const evidence = {
      browser: await browser.version(),
      channel: 'chrome',
      headless: false,
      origins: { host: state.hostOrigin, sidecarV1: state.oldOrigin, sidecarV2: state.sidecarOrigins[1], attacker: `http://127.0.0.1:${attackerPort}` },
      transport: 'local HTTP and WS only',
      cacheProbe,
      responseHeaders: observedHeaders,
      desktopSidecarMemory: sidecarMemory,
      audit,
      fixtureMetrics: metrics
    }
    const output = path.join(evidenceRoot, 'browser-automation.json')
    await import('node:fs/promises').then(fs => fs.writeFile(output, `${JSON.stringify(evidence, null, 2)}\n`))
    return output
  }
  const close = async () => {
    await browser.close()
    for (const socket of [...oldState.webSockets, ...newState.webSockets]) socket.destroy()
    await Promise.all([closeServer(hostServer), closeServer(oldServer), closeServer(newServer), closeServer(attackerServer)])
  }
  return {
    browser,
    context,
    page,
    hostOrigin: state.hostOrigin,
    oldOrigin: state.oldOrigin,
    newOrigin: state.sidecarOrigins[1],
    attackerOrigin: `http://127.0.0.1:${attackerPort}`,
    cacheProbe,
    audit: () => page.evaluate(() => window.__hostDebug.audit()),
    sidecarFrame: () => page.frame({ name: 'sidecar-frame' }),
    switchSidecar: (origin, version) => page.evaluate(({ target, build }) => window.__hostTest.switchSidecar(target, build), { target: origin, build: version }),
    captureEvidence,
    close
  }
}
