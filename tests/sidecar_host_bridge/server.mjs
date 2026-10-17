import { createHash, createPublicKey, verify as ed25519Verify } from 'node:crypto'
import { createServer } from 'node:http'
import { readFileSync, existsSync } from 'node:fs'
import path from 'node:path'

const SHA256_HEX = /^[a-f0-9]{64}$/
const sha256 = (value) => createHash('sha256').update(value).digest('hex')
const randomGrantId = () => createHash('sha256').update(`${Date.now()}:${Math.random()}:grant`).digest('hex')
const OPS = [
  'snapshot.read',
  'config.read',
  'config.apply',
  'counters.reset',
  'report-policy.read',
  'report-policy.apply',
  'video.open',
  'video.close',
  'enable.read',
  'enable.apply',
  'live.subscribe',
  'live.unsubscribe'
]
const UNAVAILABLE_REASONS = {
  'report-policy.read': 'CAPABILITY_NOT_REVIEWED',
  'report-policy.apply': 'CAPABILITY_NOT_REVIEWED',
  'video.open': 'VIDEO_MEDIA_GRANT_UNAVAILABLE',
  'video.close': 'VIDEO_MEDIA_GRANT_UNAVAILABLE',
  'enable.read': 'CAPABILITY_NOT_REVIEWED',
  'enable.apply': 'CAPABILITY_NOT_REVIEWED',
  'live.subscribe': 'CAPABILITY_NOT_REVIEWED',
  'live.unsubscribe': 'CAPABILITY_NOT_REVIEWED'
}

const baseIdentity = {
  publisher_id: 'cnt-publisher',
  app_id: 'line-counting-ui',
  publisher_key_sha256: '11'.repeat(32),
  native_tbs_sha256: '22'.repeat(32),
  ui_tbs_sha256: '33'.repeat(32),
  version_major: 1,
  version_minor: 4,
  version_patch: 2,
  host_ui_abi: 1,
  sidecar_service_generation: '42',
  host_boot_generation: 7,
  native_session_generation: 3,
  ui_generation: 11
}

const ASSET_FILES = {
  'index.html': { bytes: Buffer.from('<!doctype html><html><body>trusted-app</body></html>'), type: 'text/html; charset=utf-8' },
  'assets/app.min.js': { bytes: Buffer.from('console.log("trusted app chunk");'), type: 'text/javascript; charset=utf-8' },
  'libs/jmuxer.min.js': { bytes: Buffer.from('export const jmuxerStub = true;'), type: 'text/javascript; charset=utf-8' }
}
const ASSET_PAYLOAD_LEN = Object.values(ASSET_FILES).reduce((sum, f) => sum + f.bytes.length, 0)

const now = () => Date.now()

export function setBehavior(patch) {
  Object.assign(activeState.behavior, patch)
}

export function submitSnapshot(json) {
  activeState.snapshot = {
    json,
    config_generation: 6,
    state_revision: 5,
    arrival: now()
  }
}

export function currentGeneration() {
  return activeState.identity.sidecar_service_generation
}

export function grantCount() {
  return [...activeState.grants.values()].filter((g) => g.active && !g.revoked).length
}

let activeState = null

function createServerState() {
  return {
    identity: { ...baseIdentity },
    grants: new Map(),
    confirms: new Map(),
    snapshot: null,
    configGeneration: 6,
    pendingCommand: null,
    appSiteOrigin: null,
    resolveAppSiteOrigin: null,
    behavior: {
      failSnapshot: false,
      holdConfigApply: false,
      staleReceipt: false,
      failSave: true
    }
  }
}

export function createHostServer({ distRoot, getAppSiteOrigin }) {
  const state = createServerState()
  activeState = state
  state.resolveAppSiteOrigin = getAppSiteOrigin
  const activeDescriptor = () => {
    const resolved = state.resolveAppSiteOrigin ? state.resolveAppSiteOrigin() : null
    return {
      state: 'active',
      ...state.identity,
      ui_base_path: `/appsite/g${state.identity.sidecar_service_generation}/`,
      entry_path: 'index.html',
      app_site_origin: resolved,
      file_count: Object.keys(ASSET_FILES).length
    }
  }
  const mime = (file) => ({
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8'
  }[path.extname(file)] || 'application/octet-stream')

  const json = (response, code, payload, extraHeaders = '') => {
    response.writeHead(code, {
      'content-type': 'application/json',
      'cache-control': 'no-store',
      ...extraHeaders
    })
    response.end(JSON.stringify(payload))
  }
  const ok = (response, data, message = 'ok') => json(response, 200, { success: true, message, data })
  const fail = (response, code) => json(response, 200, { success: false, error_code: code })

  const readBody = (request) =>
    new Promise((resolve, reject) => {
      let data = ''
      request.on('data', (chunk) => (data += chunk))
      request.on('end', () => resolve(data))
      request.on('error', reject)
    })

  const grantMatches = (grant) =>
    grant.publisher_id === state.identity.publisher_id &&
    grant.app_id === state.identity.app_id &&
    grant.publisher_key_sha256 === state.identity.publisher_key_sha256 &&
    grant.native_tbs_sha256 === state.identity.native_tbs_sha256 &&
    grant.ui_tbs_sha256 === state.identity.ui_tbs_sha256 &&
    grant.sidecar_service_generation === state.identity.sidecar_service_generation &&
    grant.native_session_generation === state.identity.native_session_generation

  const seenRequests = new Map()

  const dispatch = async (request, response) => {
    const body = await readBody(request)
    const envelope = body ? JSON.parse(body) : {}
    const grant = state.grants.get(envelope.grant_id)
    const reply = (payload, revoked = false) => {
      const root = { protocol: 'ne301.sidecar.bridge.v1', request_id: envelope.request_id, op: envelope.op, revoked, result: payload }
      ok(response, root)
    }
    if (!grant || grant.revoked || now() > grant.expiresAt) {
      return fail(response, 'INVALID_REQUEST')
    }
    if (!grantMatches(grant)) {
      grant.revoked = true
      return reply({ outcome: 'REJECTED', reason: 'IDENTITY_OR_GENERATION_CHANGED' }, true)
    }
    const opKey = `${envelope.grant_id}:${envelope.op}:${envelope.request_id}`
    if (seenRequests.has(opKey)) {
      return reply({ outcome: 'REJECTED', reason: 'REPLAY' })
    }
    seenRequests.set(opKey, true)
    if (!grant.capabilities.includes(envelope.op)) {
      return reply({ outcome: 'REJECTED', reason: 'OP_NOT_IN_GRANT' })
    }
    if (UNAVAILABLE_REASONS[envelope.op]) {
      return reply({ outcome: 'UNAVAILABLE', reason: UNAVAILABLE_REASONS[envelope.op] })
    }
    if (envelope.op === 'snapshot.read') {
      if (state.behavior.failSnapshot || !state.snapshot) {
        return reply({ outcome: 'UNAVAILABLE', reason: 'SNAPSHOT_UNAVAILABLE' })
      }
      const age = now() - state.snapshot.arrival
      const freshness = age > 10000 ? 'stale' : 'fresh'
      return reply({
        outcome: freshness === 'fresh' ? 'APPLIED' : 'UNKNOWN',
        freshness,
        config_generation: state.snapshot.config_generation,
        state_revision: state.snapshot.state_revision,
        age_ms: age,
        payload: JSON.parse(state.snapshot.json)
      })
    }
    if (envelope.op === 'config.read') {
      if (!state.snapshot) {
        return reply({ outcome: 'UNAVAILABLE', reason: 'SESSION_NOT_ACTIVE' })
      }
      return reply({ outcome: 'APPLIED', config_generation: state.snapshot.config_generation })
    }
    if (envelope.op === 'config.apply' || envelope.op === 'counters.reset') {
      if (envelope.op === 'counters.reset' && !envelope.confirm_token) {
        return reply({ outcome: 'REJECTED', reason: 'ADMIN_CONFIRM_REQUIRED' })
      }
      if (envelope.op === 'counters.reset') {
        const token = state.confirms.get(envelope.confirm_token)
        if (!token || token.used || token.grantId !== grant.grantId || now() > token.expiresAt) {
          return reply({ outcome: 'REJECTED', reason: 'ADMIN_CONFIRM_REQUIRED' })
        }
        token.used = true
      }
      if (typeof envelope.expected_config_generation !== 'number' || envelope.expected_config_generation <= 0) {
        return reply({ outcome: 'REJECTED', reason: 'CONFIG_GENERATION_REQUIRED' })
      }
      if (state.behavior.holdConfigApply) {
        state.pendingCommand = envelope
        return reply({ outcome: 'UNKNOWN', reason: 'ACK_TIMEOUT' })
      }
      if (envelope.expected_config_generation !== state.configGeneration) {
        return reply({ outcome: 'CONFLICT', reason: 'STALE_CONFIG_GENERATION' })
      }
      if (state.behavior.staleReceipt) {
        return reply({ outcome: 'CONFLICT', reason: 'STALE_CONFIG_GENERATION' })
      }
      state.configGeneration += 1
      return reply({ outcome: 'APPLIED', config_generation: state.configGeneration, state_revision: 6 })
    }
    return reply({ outcome: 'REJECTED', reason: 'OP_UNKNOWN' })
  }

  return createServer(async (request, response) => {
    const url = new URL(request.url || '/', `http://${request.headers.host}`)
    const pathname = url.pathname
    if (pathname.startsWith('/appsite/')) {
      const match = /^\/appsite\/g(\d+)\/(.*)$/.exec(pathname)
      if (!match || match[1] !== state.identity.sidecar_service_generation) {
        response.writeHead(404, { 'cache-control': 'no-store' })
        response.end('rejected\n')
        return
      }
      const logical = decodeURIComponent(match[2])
      const asset = ASSET_FILES[logical]
      if (!asset || logical.includes('..')) {
        response.writeHead(404, { 'cache-control': 'no-store' })
        response.end('rejected\n')
        return
      }
      const integrity = url.searchParams.get('tamper')
      const bytes = integrity ? Buffer.from(integrity === 'empty' ? '' : 'tampered-bytes') : asset.bytes
      const digest = sha256(bytes)
      const expected = Object.keys(ASSET_FILES).find((key) => key === logical)
      if (!expected || digest !== sha256(asset.bytes) || bytes.length !== asset.bytes.length) {
        response.writeHead(404, { 'cache-control': 'no-store' })
        response.end('rejected\n')
        return
      }
      response.writeHead(200, {
        'content-type': asset.type,
        'cache-control': 'no-store',
        'x-content-type-options': 'nosniff',
        'cross-origin-resource-policy': 'cross-origin'
      })
      response.end(bytes)
      return
    }
    if (pathname === '/api/v1/app/sidecar/ui' && request.method === 'GET') {
      return ok(response, activeDescriptor(), 'Sidecar UI projection')
    }
    if (pathname === '/api/v1/app/sidecar/bridge/grant' && request.method === 'POST') {
      const descriptor = activeDescriptor()
      if (descriptor.state !== 'active') {
        return ok(response, { status: 'refused', reason: 'SIDECAR_NOT_ACTIVE' }, 'grant')
      }
      const body = JSON.parse(await readBody(request))
      const capabilities = Array.isArray(body.capabilities) ? body.capabilities.filter((op) => OPS.includes(op)) : []
      if (capabilities.length === 0) {
        return fail(response, 'INVALID_REQUEST')
      }
      if (grantCount() >= 4) {
        return ok(response, { status: 'refused', reason: 'GRANT_LIMIT_REACHED' }, 'grant')
      }
      const grantId = randomGrantId()
      const grant = {
        grantId,
        active: true,
        revoked: false,
        capabilities,
        expiresAt: now() + 300000,
        ...state.identity
      }
      state.grants.set(grantId, grant)
      return ok(response, {
        status: 'granted',
        capabilities,
        grant: {
          grant_id: grantId,
          capabilities,
          issued_at_ms: now(),
          expires_at_ms: grant.expiresAt,
          identity: {
            publisher_id: state.identity.publisher_id,
            app_id: state.identity.app_id,
            publisher_key_sha256: state.identity.publisher_key_sha256,
            native_tbs_sha256: state.identity.native_tbs_sha256,
            ui_tbs_sha256: state.identity.ui_tbs_sha256,
            version_major: state.identity.version_major,
            version_minor: state.identity.version_minor,
            version_patch: state.identity.version_patch,
            host_ui_abi: state.identity.host_ui_abi,
            sidecar_service_generation: state.identity.sidecar_service_generation,
            host_boot_generation: state.identity.host_boot_generation,
            native_session_generation: state.identity.native_session_generation,
            ui_generation: state.identity.ui_generation
          }
        }
      }, 'grant')
    }
    if (pathname === '/api/v1/app/sidecar/bridge/confirm' && request.method === 'POST') {
      const body = JSON.parse(await readBody(request))
      const grant = state.grants.get(body.grant_id)
      if (!grant || grant.revoked || body.op !== 'counters.reset') {
        return fail(response, 'INVALID_REQUEST')
      }
      const token = randomGrantId()
      state.confirms.set(token, { used: false, grantId: body.grant_id, expiresAt: now() + 120000 })
      return ok(response, { status: 'confirmed', confirm_token: token, expires_at_ms: now() + 120000 }, 'confirm')
    }
    if (pathname === '/api/v1/app/sidecar/bridge/revoke' && request.method === 'POST') {
      const body = await readBody(request)
      const parsed = body ? JSON.parse(body) : {}
      let count = 0
      for (const grant of state.grants.values()) {
        if (!grant.active) continue
        if (parsed.grant_id && grant.grantId !== parsed.grant_id) continue
        grant.revoked = true
        grant.active = false
        count += 1
      }
      return ok(response, { status: 'revoked', count }, 'revoke')
    }
    if (pathname === '/api/v1/app/sidecar/bridge' && request.method === 'POST') {
      return dispatch(request, response)
    }
    if (pathname === '/trigger-generation-switch' && request.method === 'GET') {
      state.identity.sidecar_service_generation = String(Number(state.identity.sidecar_service_generation) + 1)
      response.writeHead(200, { 'content-type': 'text/plain' })
      response.end('switched\n')
      return
    }
    const distFile = path.join(distRoot, pathname === '/' ? 'index.html' : pathname)
    if (existsSync(distFile) && !distFile.includes('app-site')) {
      response.writeHead(200, { 'content-type': mime(distFile), 'cache-control': 'no-store' })
      response.end(readFileSync(distFile))
      return
    }
    response.writeHead(404, { 'cache-control': 'no-store' })
    response.end('not found\n')
  })
}

export function createAppServer({ appRoot, allowedHostOrigin, hostOriginForFrame }) {
  const mime = (file) => ({
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8'
  }[path.extname(file)] || 'application/octet-stream')
  return createServer((request, response) => {
    const url = new URL(request.url || '/', `http://${request.headers.host}`)
    let pathname = url.pathname
    const appRoute = /^\/appsite\/g(\d+)\/(.*)$/.exec(pathname)
    if (appRoute) {
      pathname = `/${appRoute[2]}`
    }
    let file = path.join(appRoot, pathname === '/' ? 'index.html' : pathname)
    if (!file.startsWith(appRoot)) {
      response.writeHead(403)
      response.end()
      return
    }
    if (!existsSync(file)) {
      response.writeHead(404)
      response.end()
      return
    }
    const hostOrigin = url.searchParams.get('hostOrigin') || hostOriginForFrame
    response.writeHead(200, {
      'content-type': mime(file),
      'cache-control': 'no-store',
      'content-security-policy': `frame-ancestors ${allowedHostOrigin}`
    })
    let content = readFileSync(file)
    if (file.endsWith('.html')) {
      content = Buffer.from(content.toString('utf8').replace(/__HOST_ORIGIN__/g, hostOrigin))
    }
    response.end(content)
  })
}
