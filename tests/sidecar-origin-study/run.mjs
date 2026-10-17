import assert from 'node:assert/strict'
import { createHash, createHash as createSha1 } from 'node:crypto'
import { execFileSync, spawnSync } from 'node:child_process'
import { createServer as createHttpServer } from 'node:http'
import { existsSync, mkdtempSync, readFileSync, rmSync, statSync } from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { chromium } from 'playwright-core'
import { fileURLToPath } from 'node:url'
import { extractNeui } from './extract.mjs'

const root = path.dirname(fileURLToPath(import.meta.url))
const temp = mkdtempSync(path.join(os.tmpdir(), 'ne301-sidecar-origin-'))
const chromePath = process.env.SIDE_CAR_STUDY_CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
const assets = path.join(temp, 'assets')
const h264Path = path.join(temp, 'counting.h264')
const sha256 = value => createHash('sha256').update(value).digest('hex')
const bundle = path.join(root, 'input/line-crossing.neui')
const key = path.join(root, 'input/test-publisher.spki.der')
const artifact = extractNeui(bundle, key, assets)
const ffmpeg = process.env.SIDE_CAR_STUDY_FFMPEG || 'ffmpeg'
const ffmpegVersion = execFileSync(ffmpeg, ['-version'], { encoding: 'utf8' }).split('\n')[0]
const ffmpegRun = spawnSync(ffmpeg, [
  '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=25',
  '-t', '2', '-an', '-c:v', 'libx264', '-profile:v', 'baseline', '-preset', 'ultrafast', '-tune', 'zerolatency',
  '-g', '25', '-keyint_min', '25', '-sc_threshold', '0', '-x264-params', 'aud=1:repeat-headers=1',
  '-f', 'h264', '-y', h264Path,
], { encoding: 'utf8' })
if (ffmpegRun.status !== 0) throw new Error(`ffmpeg failed: ${ffmpegRun.stderr || ffmpegRun.error}`)
const h264 = readFileSync(h264Path)
const starts = []
for (let i = 0; i + 4 < h264.length;) {
  if (h264[i] === 0 && h264[i + 1] === 0 && h264[i + 2] === 0 && h264[i + 3] === 1) {
    starts.push({ at: i, nal: i + 4 })
    i += 4
  } else if (h264[i] === 0 && h264[i + 1] === 0 && h264[i + 2] === 1) {
    starts.push({ at: i, nal: i + 3 })
    i += 3
  } else i += 1
}
const aud = starts.filter(item => (h264[item.nal] & 31) === 9).map(item => item.at)
const offsets = [0, ...aud.slice(1)]
const accessUnits = offsets.map((offset, index) => h264.subarray(offset, offsets[index + 1] ?? h264.length))
assert.ok(accessUnits.length > 1)

const state = { requests: [], browserEvents: [], assetResponses: [], sockets: new Set(), cacheBuild: 'build-old', cacheRequests: 0, ports: { primary: 0, secondary: 0 } }
const hostOrigin = port => `http://host.example.test:${port}`
const siblingOrigin = port => `http://app.example.test:${port}`
const appOrigin = port => `http://app.other.test:${port}`
const mime = file => ({ '.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8', '.css': 'text/css; charset=utf-8' }[path.extname(file)] || 'application/octet-stream')

const hostHtml = `<!doctype html><html><head><meta charset="utf-8"><title>Host Admin Fixture</title></head><body><main><h1>Host Admin</h1></main><iframe id="sidecar"></iframe><iframe id="port"></iframe><iframe id="sibling"></iframe><iframe id="evil"></iframe><script>
localStorage.setItem('token','Basic dGVzdDpwYXNz');
window.__study={audit:[],portMessages:[],expectedSource:null,expectedOrigin:'',nonce:'',generation:0,port:null,navigationArmed:false,revokedByNavigation:false};
document.getElementById('port').addEventListener('load',()=>{if(window.__study.navigationArmed){window.__study.port?.close();window.__study.revokedByNavigation=true;}});
window.addEventListener('message',event=>{const m=event.data||{};if(m.type!=='study:forgery')return;if(event.source!==window.__study.expectedSource){window.__study.audit.push({decision:'deny',reason:'source',origin:event.origin});return;}if(event.origin!==window.__study.expectedOrigin){window.__study.audit.push({decision:'deny',reason:'origin',origin:event.origin});return;}if(m.nonce!==window.__study.nonce||m.generation!==window.__study.generation){window.__study.audit.push({decision:'deny',reason:'generation-or-nonce',origin:event.origin});return;}window.__study.audit.push({decision:'accept',reason:'bound'});});
</script></body></html>`

const portHtml = parent => `<!doctype html><html><head><meta charset="utf-8"><title>Port Probe</title></head><body><output>waiting</output><script>
const expectedParent=${JSON.stringify(parent)};const limit=4096;window.__port={state:'waiting',accepted:[],rejected:[],closed:false,nonce:'',generation:0,sequence:0};const deny=(reason,port)=>{window.__port.rejected.push(reason);port?.postMessage({type:'DENY',reason,nonce:window.__port.nonce,generation:window.__port.generation});};
addEventListener('message',event=>{const m=event.data||{};if(m.type!=='study:hello')return;if(event.source!==parent)return deny('source');if(event.origin!==expectedParent)return deny('origin');if(event.ports.length!==1)return deny('port-count');if(new TextEncoder().encode(JSON.stringify(m)).byteLength>limit)return deny('handshake-size',event.ports[0]);if(m.version!==1||m.status!=='ACTIVE'||m.identity?.publisher_id!=='test-publisher'||m.identity?.app_id!=='line-crossing'||!m.identity?.publisher_key_sha256||!m.identity?.native_tbs_sha256||!m.ui_tbs_sha256||!m.build_id||!m.boot_id||!m.session_id||!Number.isSafeInteger(m.ui_generation)||m.ui_generation<1||typeof m.nonce!=='string'||m.nonce.length<32||!m.permissions?.includes('lineCounting.config.read')||m.grant?.session_id!==m.session_id||m.grant?.ui_generation!==m.ui_generation||m.grant?.expires_at<=Date.now())return deny('identity-or-grant',event.ports[0]);const p=event.ports[0];window.__port={state:'ready',accepted:[],rejected:[],closed:false,nonce:m.nonce,generation:m.ui_generation,sequence:0,port:p};p.onclose=()=>{window.__port.closed=true;window.__port.state='revoked';};p.onmessage=e=>{const r=e.data||{};if(new TextEncoder().encode(JSON.stringify(r)).byteLength>limit)return deny('message-size',p);if(window.__port.state!=='ready')return deny('revoked',p);if(r.nonce!==window.__port.nonce||r.generation!==window.__port.generation)return deny('stale-generation',p);if(!Number.isSafeInteger(r.sequence)||r.sequence!==window.__port.sequence+1)return deny('sequence-replay',p);if(r.type!=='REQUEST'||r.capability!=='lineCounting.config.read'||typeof r.request_id!=='string'||r.request_id.length>64)return deny('message-shape',p);window.__port.sequence=r.sequence;window.__port.accepted.push(r.sequence);p.postMessage({type:'ACK',request_id:r.request_id,sequence:r.sequence,nonce:window.__port.nonce,generation:window.__port.generation});};p.postMessage({type:'READY',nonce:m.nonce,generation:m.ui_generation,build_id:m.build_id});});
</script></body></html>`

const siblingHtml = (parent, nonce, generation) => `<!doctype html><html><head><meta charset="utf-8"></head><body><script>parent.postMessage({type:'study:forgery',nonce:${JSON.stringify(nonce)},generation:${generation}},${JSON.stringify(parent)});</script></body></html>`
const cacheHtml = '<!doctype html><html><head><meta charset="utf-8"><script src="/cache-build.js"></script></head><body><output id="build"></output><script>document.getElementById("build").textContent=window.__cacheBuild;</script></body></html>'

const frameBuffer = (unit, sequence) => {
  const output = Buffer.alloc(64 + unit.length)
  output.writeUInt32BE(Math.floor(Date.now() / 1000) + Math.floor(sequence / 25), 12)
  unit.copy(output, 64)
  return output
}

const websocketFrame = (socket, payload) => {
  let header
  if (payload.length < 126) header = Buffer.from([0x82, payload.length])
  else if (payload.length < 65536) {
    header = Buffer.alloc(4)
    header[0] = 0x82
    header[1] = 126
    header.writeUInt16BE(payload.length, 2)
  } else {
    header = Buffer.alloc(10)
    header[0] = 0x82
    header[1] = 127
    header.writeBigUInt64BE(BigInt(payload.length), 2)
  }
  socket.write(Buffer.concat([header, payload]))
}

const handler = (request, response) => {
  const hostHeader = request.headers.host || ''
  const host = hostHeader.split(':')[0].toLowerCase()
  const url = new URL(request.url || '/', `http://${hostHeader || 'host.example.test'}`)
  state.requests.push({ host, hostHeader, path: url.pathname, cookie: request.headers.cookie || '', authorization: request.headers.authorization || '', origin: request.headers.origin || '' })
  if (url.pathname.startsWith('/api/')) {
    response.setHeader('Access-Control-Allow-Origin', '*')
    response.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
    response.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization')
    response.setHeader('Content-Type', 'application/json; charset=utf-8')
    if (request.method === 'OPTIONS') return response.end('{}')
    const ok = request.headers.authorization === 'Basic dGVzdDpwYXNz'
    response.statusCode = ok ? 200 : 401
    return response.end(JSON.stringify(ok ? { status: 'ACTIVE', app_id: 'line-crossing' } : { status: 'UNAUTHORIZED' }))
  }
  if (url.pathname === '/host.html') {
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.setHeader('Cache-Control', 'no-store')
    response.setHeader('Set-Cookie', [
      'host_only_session=host-secret; Path=/; HttpOnly; SameSite=Strict',
      'host_visible_session=host-visible-secret; Path=/; SameSite=Strict',
      'shared_domain_session=domain-secret; Domain=example.test; Path=/; HttpOnly; SameSite=Strict',
      'shared_visible_session=domain-visible-secret; Domain=example.test; Path=/; SameSite=Strict',
    ])
    response.setHeader('Content-Security-Policy', `default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-src http://host.example.test:${state.ports.primary} http://host.example.test:${state.ports.secondary} http://app.example.test:${state.ports.primary} http://app.other.test:${state.ports.primary} http://evil.other.test:${state.ports.primary}; connect-src 'self' http://host.example.test:${state.ports.primary}; object-src 'none'; base-uri 'none'`)
    return response.end(hostHtml)
  }
  if (url.pathname === '/port-target.html') {
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.setHeader('Cache-Control', 'no-store')
    response.setHeader('Content-Security-Policy', `default-src 'self'; script-src 'self' 'unsafe-inline'; frame-ancestors ${hostOrigin(state.ports.primary)}; object-src 'none'; base-uri 'none'`)
    return response.end(portHtml(hostOrigin(state.ports.primary)))
  }
  if (url.pathname === '/sibling.html') {
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.setHeader('Cache-Control', 'no-store')
    response.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self' 'unsafe-inline'; object-src 'none'; base-uri 'none'")
    return response.end(siblingHtml(hostOrigin(state.ports.primary), url.searchParams.get('nonce') || '', Number(url.searchParams.get('generation') || 0)))
  }
  if (url.pathname === '/cache-probe.html') {
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.setHeader('Cache-Control', 'no-store')
    return response.end(cacheHtml)
  }
  if (url.pathname === '/cache-build.js') {
    state.cacheRequests += 1
    const body = Buffer.from(`window.__cacheBuild=${JSON.stringify(state.cacheBuild)};`)
    const etag = `"${sha256(body)}"`
    response.setHeader('Content-Type', 'text/javascript; charset=utf-8')
    response.setHeader('Cache-Control', 'max-age=86400, public')
    response.setHeader('ETag', etag)
    if (request.headers['if-none-match'] === etag) {
      response.statusCode = 304
      return response.end()
    }
    return response.end(body)
  }
  if (url.pathname.startsWith('/sidecar/')) {
    const relative = decodeURIComponent(url.pathname.slice('/sidecar/'.length) || 'index.html')
    const normalized = path.normalize(relative)
    const target = path.resolve(assets, normalized)
    if (normalized.startsWith('..') || !target.startsWith(`${path.resolve(assets)}${path.sep}`) || !existsSync(target) || !statSync(target).isFile()) {
      response.statusCode = 404
      return response.end('not found')
    }
    const body = readFileSync(target)
    const etag = `"${sha256(body)}"`
    response.setHeader('Content-Type', mime(target))
    response.setHeader('Cache-Control', 'max-age=86400, public')
    response.setHeader('ETag', etag)
    response.setHeader('Access-Control-Allow-Origin', '*')
    response.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS')
    response.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization')
    response.setHeader('Content-Security-Policy', `default-src 'self' data: blob:; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; media-src 'self' blob:; connect-src 'self' http://host.example.test:${state.ports.primary} http://host.example.test:${state.ports.secondary} ws://host.example.test:${state.ports.primary} ws://host.example.test:${state.ports.secondary} ws://app.example.test:${state.ports.primary} ws://app.other.test:${state.ports.primary}; worker-src 'self' blob:; frame-ancestors ${hostOrigin(state.ports.primary)}; object-src 'none'; base-uri 'none'`)
    if (request.headers['if-none-match'] === etag) {
      response.statusCode = 304
      return response.end()
    }
    return response.end(body)
  }
  response.statusCode = url.pathname === '/favicon.ico' ? 204 : 404
  response.end()
}

const makeServer = () => {
  const server = createHttpServer(handler)
  server.on('upgrade', (request, socket) => {
    const key = request.headers['sec-websocket-key']
    if (request.url?.split('?')[0] !== '/stream' || typeof key !== 'string') return socket.destroy()
    const accept = createSha1('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64')
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`)
    let index = 0
    const timer = setInterval(() => {
      if (!socket.destroyed && socket.writable) websocketFrame(socket, frameBuffer(accessUnits[index++ % accessUnits.length], index))
    }, 40)
    state.sockets.add(socket)
    socket.on('error', () => undefined)
    socket.on('close', () => { clearInterval(timer); state.sockets.delete(socket) })
  })
  return server
}

const listen = server => new Promise((resolve, reject) => {
  server.once('error', reject)
  server.listen(0, '127.0.0.1', () => resolve(server.address().port))
})

const closeServer = server => new Promise(resolve => {
  if (!server?.listening) return resolve()
  server.close(() => resolve())
})

const waitFrame = async (page, prefix) => {
  const limit = Date.now() + 15000
  while (Date.now() < limit) {
    const frame = page.frames().find(item => item.url().startsWith(prefix))
    if (frame) {
      await frame.waitForLoadState('load')
      return frame
    }
    await page.waitForTimeout(50)
  }
  throw new Error(`frame did not navigate to ${prefix}`)
}

const newHostPage = async browser => {
  const context = await browser.newContext()
  const page = await context.newPage()
  page.on('console', message => { if (message.type() === 'error') state.browserEvents.push({ type: 'console', kind: message.type(), text: message.text().slice(0, 240) }) })
  page.on('pageerror', error => state.browserEvents.push({ type: 'pageerror', text: String(error).slice(0, 240) }))
  page.on('requestfailed', request => state.browserEvents.push({ type: 'requestfailed', url: request.url(), error: request.failure()?.errorText || '' }))
  page.on('response', response => { if (/\/sidecar\/assets\/(?:index-CfTnTFvt\.js|index-D-isHcyK\.css|FileSaver|websocket-init|videoWorker|jmuxer)/.test(response.url())) state.assetResponses.push({ url: response.url(), status: response.status() }) })
  await page.goto(`${hostOrigin(state.ports.primary)}/host.html`)
  await page.waitForFunction(() => localStorage.getItem('token') === 'Basic dGVzdDpwYXNz')
  return { context, page }
}

const mountFrame = async (page, id, src, sandbox) => {
  await page.evaluate(({ id, src, sandbox }) => {
    const frame = document.getElementById(id)
    frame.setAttribute('sandbox', sandbox)
    frame.src = src
  }, { id, src, sandbox })
  return waitFrame(page, src)
}

const appUiStatus = async frame => {
  await frame.waitForFunction(() => !!document.querySelector('[data-testid="lc-module"]'), null, { timeout: 15000 })
  await frame.waitForFunction(() => typeof window.saveAs === 'function', null, { timeout: 2000 }).catch(() => undefined)
  return frame.evaluate(() => ({
    moduleMounted: !!document.querySelector('[data-testid="lc-module"]'),
    appState: document.querySelector('[data-testid="lc-app-state"]')?.getAttribute('data-state') || '',
    videoState: document.querySelector('[data-testid="lc-video-state"]')?.getAttribute('data-state') || '',
    fileSaverGlobalAvailable: typeof window.saveAs === 'function',
    documentUrlOrigin: location.origin,
  }))
}

const probeWorkerAndMse = async ({ wsUrl }) => {
  const video = document.createElement('video')
  video.muted = true
  video.autoplay = true
  video.playsInline = true
  document.body.appendChild(video)
  const mse = new MediaSource()
  video.src = URL.createObjectURL(mse)
  const metrics = { workersCreated: 2, websocketOpened: false, accessUnits: 0, muxerMessages: 0, mp4Bytes: 0, appendedSegments: 0, codec: '', videoWidth: 0, videoHeight: 0, currentTime: 0, readyState: 0, error: '' }
  const pending = []
  let sourceBuffer = null
  let codec = ''
  let setupStarted = false
  const flush = () => {
    if (!sourceBuffer || sourceBuffer.updating || pending.length === 0) return
    try {
      const bytes = pending.shift()
      sourceBuffer.appendBuffer(bytes)
      metrics.appendedSegments += 1
    } catch (error) {
      metrics.error = String(error)
    }
  }
  const attachSourceBuffer = () => {
    if (setupStarted || !codec || mse.readyState !== 'open') return
    setupStarted = true
    if (!MediaSource.isTypeSupported(codec)) {
      metrics.error = `unsupported MSE codec ${codec}`
      return
    }
    try {
      sourceBuffer = mse.addSourceBuffer(codec)
      sourceBuffer.mode = 'sequence'
      sourceBuffer.addEventListener('updateend', () => {
        flush()
        try {
          if (sourceBuffer.buffered.length && sourceBuffer.buffered.end(sourceBuffer.buffered.length - 1) > 0.3 && video.paused) {
            const end = sourceBuffer.buffered.end(sourceBuffer.buffered.length - 1)
            video.currentTime = Math.max(0, end - 0.25)
            video.play().catch(error => { metrics.error = String(error) })
          }
        } catch (error) {
          metrics.error = String(error)
        }
      })
      flush()
    } catch (error) {
      metrics.error = String(error)
    }
  }
  if (mse.readyState !== 'open') mse.addEventListener('sourceopen', attachSourceBuffer, { once: true })
  const wsWorker = new Worker(new URL('./assets/websocket-init-yiJ-u6et.js', location.href), { type: 'module' })
  const decoder = new Worker(new URL('./assets/videoWorker-CwSEKqUk.js', location.href))
  wsWorker.onerror = event => { metrics.error = `websocket worker: ${event.message}` }
  decoder.onerror = event => { metrics.error = `decoder worker: ${event.message}` }
  decoder.onmessage = event => {
    const message = event.data || {}
    if (!message.codec || !message.data) return
    metrics.muxerMessages += 1
    metrics.codec = message.codec
    const bytes = message.data instanceof ArrayBuffer
      ? message.data
      : message.data.buffer.slice(message.data.byteOffset, message.data.byteOffset + message.data.byteLength)
    metrics.mp4Bytes += bytes.byteLength
    pending.push(bytes)
    if (!codec) {
      codec = message.codec
      attachSourceBuffer()
    }
    flush()
  }
  wsWorker.onmessage = event => {
    const message = event.data || {}
    if (message.type === 'open') metrics.websocketOpened = true
    if (message.type !== 'video-data' || !(message.payload instanceof ArrayBuffer) || message.payload.byteLength <= 64) return
    const access = new Uint8Array(message.payload, 64).slice().buffer
    metrics.accessUnits += 1
    decoder.postMessage({ cmd: 'video', data: access, videoTime: 40, iChannelId: 0 }, [access])
  }
  wsWorker.postMessage({ type: 'connect', url: wsUrl })
  const deadline = Date.now() + 12000
  while (Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 75))
    metrics.videoWidth = video.videoWidth
    metrics.videoHeight = video.videoHeight
    metrics.currentTime = video.currentTime
    metrics.readyState = video.readyState
    if (metrics.videoWidth === 320 && metrics.videoHeight === 180 && metrics.currentTime > 0.1) break
    if (metrics.error) break
  }
  wsWorker.postMessage({ type: 'disconnect' })
  decoder.postMessage({ cmd: 'stop' })
  await new Promise(resolve => setTimeout(resolve, 100))
  wsWorker.terminate()
  decoder.terminate()
  video.pause()
  URL.revokeObjectURL(video.src)
  video.remove()
  return metrics
}

const run = async () => {
    const report = {
    artifact,
    browser: { name: 'Google Chrome', executable: chromePath, version: '', headless: true },
    h264: { encoder: ffmpegVersion, bytes: h264.length, sha256: sha256(h264), accessUnits: accessUnits.length, dimensions: '320x180', frameRate: 25 },
    scenarios: {},
    messagePort: {},
    cache: {},
    limitations: ['device deployment hostname/DNS/TLS not observed', 'no target device or actual camera stream used', 'signed App #25 entry has no active runtime adapter; direct Worker/MSE probe does not establish UI-to-Host end-to-end playback'],
  }
  if (!existsSync(process.env.SIDE_CAR_STUDY_CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome')) {
    report.browser.status = 'NOT_RUN'
    report.networkStatus = 'UNKNOWN'
    console.log(JSON.stringify(report, null, 2))
    return report
  }
  const first = makeServer()
  const second = makeServer()
  let browser
  const contexts = []
  try {
    state.ports.primary = await listen(first)
    state.ports.secondary = await listen(second)
    const resolver = `MAP host.example.test 127.0.0.1,MAP app.example.test 127.0.0.1,MAP app.other.test 127.0.0.1,MAP evil.other.test 127.0.0.1`
    browser = await chromium.launch({ executablePath: chromePath, headless: true, args: [`--host-resolver-rules=${resolver}`, '--no-first-run', '--no-default-browser-check'] })
    report.browser.version = browser.version()
    report.browser.status = 'RUN'

    const opaque = await newHostPage(browser)
    contexts.push(opaque.context)
    const opaqueFrame = await mountFrame(opaque.page, 'sidecar', `${hostOrigin(state.ports.primary)}/sidecar/index.html`, 'allow-scripts')
    const opaqueUi = await appUiStatus(opaqueFrame)
    const opaqueAccess = await opaqueFrame.evaluate(async target => {
      let parentDom = 'allowed'
      let storage = 'allowed'
      let cookie = ''
      try { void parent.document.title } catch (error) { parentDom = error.name }
      try { localStorage.getItem('token') } catch (error) { storage = error.name }
      try { cookie = document.cookie } catch (error) { cookie = error.name }
      let apiStatus = 0
      try { apiStatus = (await fetch(`${target}/api/v1/app/sidecar/installed`, { credentials: 'omit' })).status } catch { apiStatus = -1 }
      const attempt = async (url, options) => new Promise(resolve => {
        let worker
        try { worker = new Worker(url, options) } catch (error) { return resolve({ state: 'blocked', name: error.name, message: error.message }) }
        const timer = setTimeout(() => { worker.terminate(); resolve({ state: 'started' }) }, 500)
        worker.onerror = event => { clearTimeout(timer); worker.terminate(); resolve({ state: 'blocked', name: event.error?.name || 'Error', message: event.message }) }
      })
      const workers = await Promise.all([
        attempt(new URL('./assets/websocket-init-yiJ-u6et.js', location.href), { type: 'module' }),
        attempt(new URL('./assets/videoWorker-CwSEKqUk.js', location.href)),
      ])
      return { parentDom, storage, cookie, apiStatus, workers }
    }, hostOrigin(state.ports.primary))
    report.scenarios.opaqueOrigin = { sandbox: 'allow-scripts', ui: opaqueUi, access: opaqueAccess, expectedWorkerStart: false }

    const same = await newHostPage(browser)
    contexts.push(same.context)
    const sameFrame = await mountFrame(same.page, 'sidecar', `${hostOrigin(state.ports.primary)}/sidecar/index.html`, 'allow-scripts allow-same-origin')
    await appUiStatus(sameFrame)
    const escape = await sameFrame.evaluate(async () => {
      const token = localStorage.getItem('token')
      const title = parent.document.title
      const cookie = document.cookie
      const result = await fetch('/api/v1/app/sidecar/installed', { headers: { Authorization: token } })
      frameElement.removeAttribute('sandbox')
      parent.document.body.dataset.sandboxRemoved = 'yes'
      return { tokenReadable: !!token, parentTitleReadable: !!title, cookie, apiStatus: result.status, sandboxRemoved: frameElement.getAttribute('sandbox') === null }
    })
    await same.page.evaluate(() => sessionStorage.setItem('study-reload', '1'))
    const navigation = same.page.waitForNavigation({ waitUntil: 'domcontentloaded' })
    await sameFrame.evaluate(() => parent.location.reload()).catch(() => undefined)
    await navigation
    const reloaded = await same.page.evaluate(() => sessionStorage.getItem('study-reload') === '1')
    report.scenarios.sameOriginEscape = { sandbox: 'allow-scripts allow-same-origin', ...escape, topReloadObserved: reloaded }

    const portOnly = await newHostPage(browser)
    contexts.push(portOnly.context)
    const portFrame = await mountFrame(portOnly.page, 'sidecar', `http://host.example.test:${state.ports.secondary}/sidecar/index.html`, 'allow-scripts allow-same-origin')
    await appUiStatus(portFrame)
    const portAccess = await portFrame.evaluate(async target => {
      let parentDom = 'allowed'
      try { void parent.document.title } catch (error) { parentDom = error.name }
      let token = null
      try { token = localStorage.getItem('token') } catch (error) { token = 'blocked' }
      return { parentDom, localStorageTokenVisible: !!token, cookie: document.cookie, apiStatus: (await fetch(`${target}/api/v1/app/sidecar/installed`, { credentials: 'omit' })).status }
    }, hostOrigin(state.ports.primary))
  const portAssetRequest = state.requests.find(request => request.path === '/sidecar/index.html' && request.hostHeader === `host.example.test:${state.ports.secondary}` && request.cookie)
    report.scenarios.portOnly = { sandbox: 'allow-scripts allow-same-origin', origin: 'same hostname, different port', access: portAccess, cookieOnAppAssetRequest: portAssetRequest?.cookie || '' }

    const sibling = await newHostPage(browser)
    contexts.push(sibling.context)
    const siblingApp = await mountFrame(sibling.page, 'sidecar', `${siblingOrigin(state.ports.primary)}/sidecar/index.html`, 'allow-scripts allow-same-origin')
    const siblingUi = await appUiStatus(siblingApp)
    const siblingAccess = await siblingApp.evaluate(async target => {
      let parentDom = 'allowed'
      try { void parent.document.title } catch (error) { parentDom = error.name }
      const response = await fetch(`${target}/api/v1/app/sidecar/installed`, { credentials: 'omit' })
      return { parentDom, token: localStorage.getItem('token'), cookie: document.cookie, apiStatus: response.status }
    }, hostOrigin(state.ports.primary))
    const siblingAssetRequest = state.requests.find(request => request.hostHeader === `app.example.test:${state.ports.primary}` && request.path === '/sidecar/index.html')
    report.scenarios.sameSiteSiblingHost = { sandbox: 'allow-scripts allow-same-origin', ui: siblingUi, access: siblingAccess, cookieOnAppAssetRequest: siblingAssetRequest?.cookie || '' }

    const independent = await newHostPage(browser)
    contexts.push(independent.context)
    const appFrame = await mountFrame(independent.page, 'sidecar', `${appOrigin(state.ports.primary)}/sidecar/index.html`, 'allow-scripts allow-same-origin')
    const independentUi = await appUiStatus(appFrame)
    const independentAccess = await appFrame.evaluate(async target => {
      let parentDom = 'allowed'
      try { void parent.document.title } catch (error) { parentDom = error.name }
      const token = localStorage.getItem('token')
      const api = await fetch(`${target}/api/v1/app/sidecar/installed`, { credentials: 'omit' })
      return { parentDom, tokenVisible: !!token, cookie: document.cookie, apiStatus: api.status }
    }, hostOrigin(state.ports.primary))
    const resourceStart = state.requests.length
    const workerMse = await appFrame.evaluate(probeWorkerAndMse, { wsUrl: `ws://app.other.test:${state.ports.primary}/stream` })
    const resources = state.requests.slice(resourceStart).filter(request => request.path.startsWith('/sidecar/'))
    report.scenarios.independentOrigin = {
      origin: 'different hostname and site, same test server IP and port',
      sandbox: 'allow-scripts allow-same-origin',
      ui: independentUi,
      access: independentAccess,
      workerMse,
      resourcePaths: [...new Set(resources.map(request => request.path))].sort(),
      workerAssetRequests: resources.filter(request => /websocket-init|videoWorker|jmuxer/.test(request.path)).map(request => request.path),
    }

    const signedHandshake = await independent.page.evaluate(async targetOrigin => {
      const iframe = document.getElementById('sidecar')
      const channel = new MessageChannel()
      const messages = []
      channel.port1.onmessage = event => messages.push(event.data)
      iframe.contentWindow.postMessage({ type: 'study:hello', version: 1, status: 'ACTIVE', nonce: 'a'.repeat(64), ui_generation: 1, app_id: 'line-crossing' }, targetOrigin, [channel.port2])
      await new Promise(resolve => setTimeout(resolve, 500))
      channel.port1.close()
      return messages
    }, appOrigin(state.ports.primary))
    report.scenarios.signedAppHandshake = { acknowledged: signedHandshake.length > 0, messages: signedHandshake }

    const portContext = await browser.newContext()
    contexts.push(portContext)
    const portPage = await portContext.newPage()
    await portPage.goto(`${hostOrigin(state.ports.primary)}/host.html`)
    const portTarget = await mountFrame(portPage, 'port', `${appOrigin(state.ports.primary)}/port-target.html`, 'allow-scripts allow-same-origin')
    const nonce = createHash('sha256').update(String(Math.random()) + String(Date.now())).digest('hex')
    const generation = 7
    await portPage.evaluate(({ appOrigin, nonce, generation, artifact }) => {
      const frame = document.getElementById('port')
      window.__study.expectedSource = frame.contentWindow
      window.__study.expectedOrigin = appOrigin
      window.__study.nonce = nonce
      window.__study.generation = generation
      window.__study.portFrame = frame
      const channel = new MessageChannel()
      window.__study.port = channel.port1
      channel.port1.onmessage = event => window.__study.portMessages.push(event.data)
      const message = {
        type: 'study:hello', version: 1, status: 'ACTIVE', nonce, ui_generation: generation,
        identity: { publisher_id: artifact.publisherId, app_id: artifact.appId, publisher_key_sha256: artifact.publisherKeySha256, native_tbs_sha256: artifact.nativeTbsSha256 },
        ui_tbs_sha256: artifact.uiTbsSha256, build_id: artifact.bundleSha256, boot_id: 'test-boot', session_id: 'test-session',
        permissions: ['lineCounting.config.read'], grant: { session_id: 'test-session', ui_generation: generation, expires_at: Date.now() + 10000 },
      }
      frame.contentWindow.postMessage(message, appOrigin, [channel.port2])
    }, { appOrigin: appOrigin(state.ports.primary), nonce, generation, artifact })
    await portPage.waitForFunction(() => window.__study.portMessages.some(message => message.type === 'READY'))
    await portPage.evaluate(() => window.__study.port.postMessage({ type: 'REQUEST', request_id: 'read-1', capability: 'lineCounting.config.read', sequence: 1, nonce: window.__study.nonce, generation: window.__study.generation }))
    await portPage.waitForFunction(() => window.__study.portMessages.some(message => message.type === 'ACK' && message.sequence === 1))
    await portPage.evaluate(() => window.__study.port.postMessage({ type: 'REQUEST', request_id: 'stale-generation', capability: 'lineCounting.config.read', sequence: 2, nonce: window.__study.nonce, generation: window.__study.generation - 1 }))
    await portPage.waitForFunction(() => window.__study.portMessages.some(message => message.type === 'DENY' && message.reason === 'stale-generation'))
    await portPage.evaluate(() => window.__study.port.postMessage({ type: 'REQUEST', request_id: 'read-1-replay', capability: 'lineCounting.config.read', sequence: 1, nonce: window.__study.nonce, generation: window.__study.generation }))
    await portPage.waitForFunction(() => window.__study.portMessages.some(message => message.type === 'DENY' && message.reason === 'sequence-replay'))
    await portPage.evaluate(() => window.__study.port.postMessage({ type: 'REQUEST', request_id: 'unauthorized-capability', capability: 'host.admin.delete', sequence: 2, nonce: window.__study.nonce, generation: window.__study.generation }))
    await portPage.waitForFunction(() => window.__study.portMessages.some(message => message.type === 'DENY' && message.reason === 'message-shape'))
    await portPage.evaluate(() => window.__study.port.postMessage({ type: 'REQUEST', request_id: 'too-large', capability: 'lineCounting.config.read', sequence: 2, nonce: window.__study.nonce, generation: window.__study.generation, data: 'x'.repeat(5000) }))
    await portPage.waitForFunction(() => window.__study.portMessages.some(message => message.type === 'DENY' && message.reason === 'message-size'))
    await portPage.evaluate(({ origin, nonce, generation, artifact }) => {
      window.__study.invalidHandshakeMessages = []
      const send = (overrides, marker) => {
        const channel = new MessageChannel()
        channel.port1.onmessage = event => { window.__study.invalidHandshakeMessages.push({ marker, ...event.data }); channel.port1.close() }
        const message = {
          type: 'study:hello', version: 1, status: 'ACTIVE', nonce, ui_generation: generation,
          identity: { publisher_id: artifact.publisherId, app_id: artifact.appId, publisher_key_sha256: artifact.publisherKeySha256, native_tbs_sha256: artifact.nativeTbsSha256 },
          ui_tbs_sha256: artifact.uiTbsSha256, build_id: artifact.bundleSha256, boot_id: 'test-boot', session_id: `invalid-${marker}`,
          permissions: ['lineCounting.config.read'], grant: { session_id: `invalid-${marker}`, ui_generation: generation, expires_at: Date.now() + 10000 },
        }
        Object.assign(message, overrides)
        document.getElementById('port').contentWindow.postMessage(message, origin, [channel.port2])
      }
      send({ status: 'PREPARED' }, 'inactive')
      send({ permissions: [], grant: { session_id: `invalid-expired`, ui_generation: generation, expires_at: Date.now() - 1 } }, 'expired-and-ungranted')
      send({ data: 'x'.repeat(5000) }, 'oversized-handshake')
    }, { origin: appOrigin(state.ports.primary), nonce, generation, artifact })
    await portPage.waitForFunction(() => window.__study.invalidHandshakeMessages.length === 3)
    const handshakeDenials = await portPage.evaluate(() => window.__study.invalidHandshakeMessages)
    const acceptedBeforeClose = await portPage.evaluate(() => window.__study.portMessages.length)
    await portPage.evaluate(() => window.__study.port.close())
    const directPortResult = await portTarget.evaluate(() => ({ state: window.__port.state, accepted: window.__port.accepted, rejected: window.__port.rejected }))
    await portPage.evaluate(() => window.__study.port?.postMessage({ type: 'REQUEST', request_id: 'after-close', capability: 'lineCounting.config.read', sequence: 2, nonce: window.__study.nonce, generation: window.__study.generation }))
    await portPage.waitForTimeout(200)
    const acceptedAfterClose = await portTarget.evaluate(() => window.__port.accepted)
    await portTarget.evaluate(({ parentOrigin, generation }) => parent.postMessage({ type: 'study:forgery', nonce: '0'.repeat(64), generation }, parentOrigin), { parentOrigin: hostOrigin(state.ports.primary), generation })
    await portPage.evaluate(({ origin, nonce, generation }) => {
      const sibling = document.getElementById('sibling')
      sibling.setAttribute('sandbox', 'allow-scripts')
      sibling.src = `${origin}/sibling.html?nonce=${encodeURIComponent(nonce)}&generation=${generation}`
      document.getElementById('evil').src = `${origin.replace('app.other.test', 'evil.other.test')}/sibling.html?nonce=${encodeURIComponent(nonce)}&generation=${generation}`
    }, { origin: appOrigin(state.ports.primary), nonce, generation })
    await portPage.waitForFunction(() => window.__study.audit.length >= 3)
    const secondNonce = createHash('sha256').update(`${nonce}:navigation`).digest('hex')
    const secondGeneration = generation + 1
    await portPage.evaluate(({ origin, nonce, generation, artifact }) => {
      const frame = document.getElementById('port')
      window.__study.nonce = nonce
      window.__study.generation = generation
      const channel = new MessageChannel()
      window.__study.port = channel.port1
      channel.port1.onmessage = event => window.__study.portMessages.push(event.data)
      frame.contentWindow.postMessage({
        type: 'study:hello', version: 1, status: 'ACTIVE', nonce, ui_generation: generation,
        identity: { publisher_id: artifact.publisherId, app_id: artifact.appId, publisher_key_sha256: artifact.publisherKeySha256, native_tbs_sha256: artifact.nativeTbsSha256 },
        ui_tbs_sha256: artifact.uiTbsSha256, build_id: artifact.bundleSha256, boot_id: 'test-boot', session_id: 'test-session-2',
        permissions: ['lineCounting.config.read'], grant: { session_id: 'test-session-2', ui_generation: generation, expires_at: Date.now() + 10000 },
      }, origin, [channel.port2])
    }, { origin: appOrigin(state.ports.primary), nonce: secondNonce, generation: secondGeneration, artifact })
    await portPage.waitForFunction(() => window.__study.portMessages.some(message => message.type === 'READY' && message.generation === 8))
    await portPage.evaluate(({ origin, nonce, generation }) => {
      const frame = document.getElementById('port')
      frame.src = `${origin.replace('app.other.test', 'evil.other.test')}/sibling.html?nonce=${encodeURIComponent(nonce)}&generation=${generation}`
    }, { origin: appOrigin(state.ports.primary), nonce: secondNonce, generation: secondGeneration })
    await waitFrame(portPage, `http://evil.other.test:${state.ports.primary}/sibling.html`)
    await portPage.waitForFunction(() => window.__study.audit.some(entry => entry.reason === 'origin'))
    const forgeryAudit = await portPage.evaluate(() => window.__study.audit)
    await portPage.evaluate(origin => { window.__study.navigationArmed = true; window.__study.portFrame.src = `${origin}/sidecar/index.html` }, appOrigin(state.ports.primary))
    await waitFrame(portPage, `${appOrigin(state.ports.primary)}/sidecar/index.html`)
    await portPage.waitForFunction(() => window.__study.revokedByNavigation)
    report.messagePort = { browserTransport: 'PASS', probeSchemaOnly: true, readyAck: true, requestAck: true, staleGenerationRejected: true, duplicateSequenceRejected: true, unauthorizedCapabilityRejected: true, oversizedMessageRejected: true, invalidHandshakesRejected: handshakeDenials, explicitClose: true, noDeliveryAfterClose: JSON.stringify(acceptedAfterClose) === JSON.stringify(directPortResult.accepted), acceptedBeforeClose, acceptedAfterClose, navigationRevoked: true, directFixture: directPortResult, siblingForgeryAudit: forgeryAudit, signedAppAdapterAck: report.scenarios.signedAppHandshake.acknowledged }

    const cachePage = await independent.context.newPage()
    await cachePage.goto(`${appOrigin(state.ports.primary)}/cache-probe.html`)
    const firstBuild = await cachePage.evaluate(() => document.getElementById('build').textContent)
    const before = state.cacheRequests
    state.cacheBuild = 'build-current'
    const cachedContent = await cachePage.evaluate(async () => await (await fetch('/cache-build.js', { cache: 'force-cache' })).text())
    const staleBuild = /"([^"]+)"/.exec(cachedContent)?.[1] || ''
    const replayAccepted = staleBuild === state.cacheBuild
    report.cache = { responsePolicy: 'Cache-Control: max-age=86400, public', firstBuild, activeBuildAfterUpdate: state.cacheBuild, replayedBuild: staleBuild, requestCountBeforeReplay: before, requestCountAfterReplay: state.cacheRequests, generationReplayAccepted: replayAccepted, generationReplayRejected: !replayAccepted }

    report.requests = state.requests.filter(request => request.path === '/api/v1/app/sidecar/installed' || /\/sidecar\/(?:index\.html|assets\/(?:index-CfTnTFvt\.js|index-D-isHcyK\.css|FileSaver|websocket-init|videoWorker|jmuxer))/.test(request.path)).map(request => ({ host: request.host, hostHeader: request.hostHeader, path: request.path, cookieNames: request.cookie.split(';').map(part => part.trim().split('=')[0]).filter(Boolean), hasAuthorization: !!request.authorization, origin: request.origin }))
    report.assetResponses = state.assetResponses
    report.browserEvents = state.browserEvents
    report.summary = {
      opaqueWorkersBlocked: opaqueAccess.workers.every(worker => worker.state === 'blocked'),
      sameOriginSandboxEscaped: escape.parentTitleReadable && escape.tokenReadable && escape.sandboxRemoved && reloaded,
      portCookieShared: (portAssetRequest?.cookie || '').includes('host_only_session='),
      domainCookieSharedWithSibling: (siblingAssetRequest?.cookie || '').includes('shared_domain_session='),
      separateSiteOriginWorked: workerMse.videoWidth === 320 && workerMse.videoHeight === 180 && workerMse.currentTime > 0.1,
      targetDeployment: 'UNKNOWN',
      readyDecision: 'BLOCKED',
    }
    assert.equal(report.summary.opaqueWorkersBlocked, true)
    assert.equal(report.summary.sameOriginSandboxEscaped, true)
    assert.equal(report.summary.portCookieShared, true)
    assert.equal(report.summary.domainCookieSharedWithSibling, true)
    assert.equal(report.summary.separateSiteOriginWorked, true)
    assert.ok(report.scenarios.independentOrigin.workerAssetRequests.some(asset => asset.endsWith('/websocket-init-yiJ-u6et.js')))
    assert.ok(report.scenarios.independentOrigin.workerAssetRequests.some(asset => asset.endsWith('/videoWorker-CwSEKqUk.js')))
    assert.ok(report.scenarios.independentOrigin.workerAssetRequests.some(asset => asset.endsWith('/jmuxer.min.js')))
    assert.ok(state.assetResponses.some(response => response.url.includes('/FileSaver.min-D428eXCv.js') && response.status === 200))
    assert.equal(report.messagePort.noDeliveryAfterClose, true)
    assert.equal(report.messagePort.invalidHandshakesRejected.length, 3)
    assert.ok(report.messagePort.siblingForgeryAudit.some(entry => entry.reason === 'generation-or-nonce'))
    assert.ok(report.messagePort.siblingForgeryAudit.some(entry => entry.reason === 'origin'))
    assert.equal(report.cache.generationReplayRejected, true)
    assert.equal(report.scenarios.signedAppHandshake.acknowledged, false)
    console.log(JSON.stringify(report, null, 2))
    return report
  } finally {
    for (const socket of state.sockets) socket.destroy()
    for (const context of contexts) await context.close().catch(() => undefined)
    if (browser) await browser.close().catch(() => undefined)
    await closeServer(first)
    await closeServer(second)
    rmSync(temp, { recursive: true, force: true })
  }
}

run().catch(error => {
  console.error(JSON.stringify({ status: 'HARNESS_ERROR', message: String(error), stack: error?.stack || '' }, null, 2))
  rmSync(temp, { recursive: true, force: true })
  process.exitCode = 1
})
