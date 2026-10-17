const SIDECAR_BRIDGE_PROTOCOL = 'ne301.sidecar.bridge.v1'

const statusBox = document.getElementById('app-status')
const snapshotBox = document.getElementById('snapshot-outcome')
const workerBox = document.getElementById('workers')
const query = new URLSearchParams(window.location.search)
const mode = query.get('mode') ?? 'normal'
const hostOrigin = query.get('hostOrigin') ?? ''

let port = null
let granted = false
let nextRequestId = 1
let lastSeqSent = 0

function spawnWorkers() {
  let ready = 0
  const bump = () => {
    ready += 1
    workerBox.textContent = `${ready}/2`
  }
  for (let i = 0; i < 2; i++) {
    try {
      void bump
      const code = `self.onmessage=e=>{if(e.data==='ping'){self.postMessage('ready-${i}')}}`
      const url = URL.createObjectURL(new Blob([code], { type: 'text/javascript' }))
      const worker = new Worker(url)
      worker.onmessage = (event) => {
        if (event.data === `ready-${i}`) {
          bump()
        }
      }
      worker.onerror = (event) => {
        window.__workerError = `worker-${i}: ${String(event.message || event.type || 'error').slice(0, 120)}`
      }
      worker.postMessage('ping')
    } catch (error) {
      window.__workerError = `worker-${i}: ${String(error).slice(0, 120)}`
    }
  }
}

function send(op, extra = {}) {
  const request = {
    protocol: SIDECAR_BRIDGE_PROTOCOL,
    type: 'REQUEST',
    seq: ++lastSeqSent,
    grant_id: currentGrantId(),
    identity: currentIdentity(),
    request_id: nextRequestId++,
    op,
    ...extra,
  }
  port?.postMessage(request)
  return request.request_id
}

let grantInfo = { grant_id: '', identity: null }
const currentGrantId = () => grantInfo.grant_id
const currentIdentity = () => grantInfo.identity

function applyModeFlow() {
  if (mode === 'replay') {
    const first = send('snapshot.read')
    setTimeout(() => {
      const request = {
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'REQUEST',
        seq: ++lastSeqSent,
        grant_id: grantInfo.grant_id,
        identity: grantInfo.identity,
        request_id: first,
        op: 'snapshot.read',
      }
      port?.postMessage(request)
    }, 150)
  }
  if (mode === 'config-apply') {
    send('config.apply', {
      expected_config_generation: 6,
      payload: { line: { y: 128 } },
    })
  }
  if (mode === 'config-apply-unconfirmed') {
    send('counters.reset', { expected_config_generation: 6 })
  }
  if (mode === 'policy-save') {
    send('report-policy.apply', { payload: { mqtt: { topic: 'x' } } })
  }
  if (mode === 'generation-switch') {
    send('snapshot.read')
    fetch(`${hostOrigin}/trigger-generation-switch`).catch(() => undefined)
    setTimeout(() => send('snapshot.read'), 400)
  }
  if (mode === 'forged-message') {
    setTimeout(() => {
      window.parent.postMessage(
        {
          protocol: SIDECAR_BRIDGE_PROTOCOL,
          type: 'REQUEST',
          seq: 9999,
          grant_id: 'f'.repeat(64),
          identity: grantInfo.identity,
          request_id: 500,
          op: 'config.apply',
        },
        hostOrigin || '*',
      )
    }, 150)
  }
}

window.addEventListener('message', (event) => {
  if (event.origin !== hostOrigin) {
    statusBox.textContent = `rejected-origin:${event.origin}`
    return
  }
  const message = event.data
  if (!message || message.protocol !== SIDECAR_BRIDGE_PROTOCOL) {
    statusBox.textContent = 'rejected-envelope'
    return
  }
  if (message.type === 'HELLO' && event.source === window.parent && !port) {
    if (typeof message.seq !== 'number' || typeof message.grant_id !== 'string') {
      statusBox.textContent = 'rejected-hello'
      return
    }
    port = event.ports[0]
    if (!port) {
      statusBox.textContent = 'rejected-no-port'
      return
    }
    statusBox.textContent = 'hello-accepted'
    try {
      port.postMessage({ probe: 'ready' })
    } catch (err) {
      console.log('[app] probe post failed', String(err).slice(0, 100))
    }
    port.onmessage = (portEvent) => {
      const inbound = portEvent.data
      if (!inbound || inbound.protocol !== SIDECAR_BRIDGE_PROTOCOL) {
        statusBox.textContent = 'rejected-port-envelope'
        return
      }
      if (inbound.type === 'GRANT') {
        granted = true
        grantInfo = { grant_id: inbound.grant_id, identity: inbound.identity }
        statusBox.textContent = 'granted'
        spawnWorkers()
        send('snapshot.read')
        applyModeFlow()
      }
      if (inbound.type === 'RESULT') {
        if (inbound.op === 'snapshot.read') {
          snapshotBox.textContent = JSON.stringify({
            outcome: inbound.outcome,
            freshness: inbound.freshness ?? '',
            reason: inbound.reason ?? '',
          })
        }
        if (inbound.op === 'config.apply') {
          const box = document.getElementById('config-apply')
          if (box) box.textContent = JSON.stringify({ outcome: inbound.outcome, reason: inbound.reason ?? '' })
        }
        if (inbound.op === 'counters.reset') {
          const box = document.getElementById('counters-reset')
          if (box) box.textContent = JSON.stringify({ outcome: inbound.outcome, reason: inbound.reason ?? '' })
        }
        if (inbound.op === 'report-policy.apply') {
          const box = document.getElementById('policy-save')
          if (box) box.textContent = JSON.stringify({ outcome: inbound.outcome, reason: inbound.reason ?? '' })
        }
      }
      if (inbound.type === 'REVOKE') {
        statusBox.textContent = `revoked:${inbound.reason}`
        granted = false
        try {
          port?.close()
        } catch {}
        port = null
      }
    }
    return
  }
  if (message.type !== 'HELLO' && !port) {
    statusBox.textContent = 'rejected-before-hello'
  }
})
