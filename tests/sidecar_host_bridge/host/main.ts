import {
  SidecarBroker,
  isCrossSite,
  type SidecarGrant,
  type SidecarOp,
  type SidecarUiProjection,
} from '@/services/sidecarBridge'

const status = document.getElementById('status') as HTMLElement
const projectionBox = document.getElementById('projection') as HTMLElement
const snapshotBox = document.getElementById('snapshot') as HTMLElement
const revokeBox = document.getElementById('revoke') as HTMLElement
const frameSlot = document.getElementById('frame-slot') as HTMLElement

const query = new URLSearchParams(window.location.search)
const timeoutMs = query.get('timeoutMs') ? Number(query.get('timeoutMs')) : undefined
const mode = query.get('mode') ?? 'normal'
const appOriginOverride = query.get('appOrigin')

window.addEventListener('message', (event) => {
  const expected = query.get('appOrigin')
  if (!expected || event.origin !== expected) {
    return
  }
  const marker = document.getElementById('forged')
  if (marker) {
    marker.textContent = 'ignored-window-message'
  }
})

async function postJson(path: string, body: unknown): Promise<any> {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body),
  })
  return response.json()
}

async function serverCall(
  op: SidecarOp,
  request: {
    grant_id: string
    request_id: number
    payload?: unknown
    expected_config_generation?: number
    confirm_token?: string
  },
): Promise<{
  outcome: any
  reason?: string
  freshness?: string
  config_generation?: number
  state_revision?: number
  age_ms?: number
  payload?: unknown
  revoked?: boolean
}> {
  const envelope = await postJson('/api/v1/app/sidecar/bridge', { op, ...request })
  if (!envelope || envelope.success !== true) {
    return {
      outcome: 'UNKNOWN',
      reason: envelope?.error_code || 'TRANSPORT_FAILED',
      revoked: true,
    }
  }
  const data = envelope.data || {}
  const result = data.result || {}
  return {
    outcome: result.outcome ?? 'UNKNOWN',
    reason: result.reason,
    freshness: result.freshness,
    config_generation: result.config_generation,
    state_revision: result.state_revision,
    age_ms: result.age_ms,
    payload: result.payload,
    revoked: data.revoked === true,
  }
}

async function main(): Promise<void> {
  const projectionResponse = await fetch('/api/v1/app/sidecar/ui')
  const projectionEnvelope = await projectionResponse.json()
  const projection = (projectionEnvelope.data ?? {}) as SidecarUiProjection
  projectionBox.textContent = JSON.stringify(projection)
  if (projection.state !== 'active' || !projection.app_site_origin) {
    status.textContent = 'unavailable'
    return
  }
  const appOrigin = appOriginOverride ?? String(projection.app_site_origin)
  const adminOrigin = window.location.origin
  if (adminOrigin === appOrigin || !isCrossSite(adminOrigin, appOrigin)) {
    status.textContent = 'blocked-same-site'
    return
  }
  status.textContent = 'creating-frame'
  const iframe = document.createElement('iframe')
  iframe.setAttribute('sandbox', 'allow-scripts allow-same-origin allow-downloads')
  iframe.src = `${appOrigin}${projection.ui_base_path ?? '/appsite/g1/'}index.html?mode=${encodeURIComponent(mode)}&hostOrigin=${encodeURIComponent(adminOrigin)}`
  iframe.setAttribute('data-testid', 'host-frame')
  iframe.title = 'Line Crossing (App)'
  frameSlot.appendChild(iframe)

  await new Promise<void>((resolve) => {
    iframe.addEventListener('load', () => resolve(), { once: true })
  })

  const grantEnvelope = await postJson('/api/v1/app/sidecar/bridge/grant', {
    capabilities: [
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
      'live.unsubscribe',
    ],
  })
  if (!grantEnvelope || grantEnvelope.success !== true || grantEnvelope.data?.status !== 'granted') {
    status.textContent = 'grant-refused'
    iframe.remove()
    return
  }
  const grantData = grantEnvelope.data.grant
  const grant: SidecarGrant = {
    grant_id: String(grantData.grant_id),
    expires_at_ms: Number(grantData.expires_at_ms),
    issued_at_ms: Number(grantData.issued_at_ms),
    capabilities: (grantEnvelope.data.capabilities ?? grantData.capabilities) as SidecarGrant['capabilities'],
    identity: grantData.identity,
  }
  status.textContent = 'granted'

  const broker = new SidecarBroker({
    appWindow: iframe.contentWindow as Window,
    appOrigin: new URL(appOrigin).origin,
    grant,
    serverCall,
    timeoutMs,
    onResult: (result) => {
      if (result.op === 'snapshot.read') {
        snapshotBox.textContent = JSON.stringify({
          outcome: result.outcome,
          freshness: result.freshness,
          payload: result.payload,
        })
      }
      if (result.op === 'config.apply') {
        const box = document.getElementById('config-apply') as HTMLElement | null
        if (box) {
          box.textContent = JSON.stringify({ outcome: result.outcome, reason: result.reason })
        }
      }
      if (result.op === 'counters.reset') {
        const box = document.getElementById('counters-reset') as HTMLElement | null
        if (box) {
          box.textContent = JSON.stringify({ outcome: result.outcome, reason: result.reason })
        }
      }
      if (result.op === 'report-policy.apply') {
        const box = document.getElementById('policy-save') as HTMLElement | null
        if (box) {
          box.textContent = JSON.stringify({ outcome: result.outcome, reason: result.reason })
        }
      }
      ;(window as any).lastResult = result
    },
    onRevoke: (reason) => {
      revokeBox.textContent = reason
      revokeBox.hidden = false
      iframe.remove()
      status.textContent = 'revoked'
    },
  })
  ;(window as any).hostBroker = broker
  broker.start()
}

void main()
