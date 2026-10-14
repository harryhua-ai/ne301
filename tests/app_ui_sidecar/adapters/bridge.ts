export type SidecarCapability =
  | 'peopleCounting.config.read'
  | 'peopleCounting.config.write'
  | 'peopleCounting.stats.read'
  | 'peopleCounting.backlog.read'
  | 'peopleCounting.stats.reset'
  | 'peopleCounting.video.open'
  | 'peopleCounting.video.close'

type PendingRequest = {
  resolve: (value: unknown) => void
  reject: (reason: Error & { code?: string }) => void
  timeout: number
}

const pending = new Map<string, PendingRequest>()
let hostOrigin = ''
let nonce = ''
let sequence = 0
let readyResolve: (() => void) | undefined
let ready = new Promise<void>(resolve => { readyResolve = resolve })

const receive = (event: MessageEvent) => {
  if (event.source !== window.parent) return
  const message = event.data
  if (!message || typeof message !== 'object') return
  if (message.type === 'bootstrap') {
    if (typeof message.nonce !== 'string' || message.nonce.length < 16) return
    hostOrigin = event.origin
    nonce = message.nonce
    window.__fixtureBridgeNonce = nonce
    window.__fixtureHostOrigin = hostOrigin
    window.parent.postMessage({ type: 'ready', nonce }, hostOrigin)
    readyResolve?.()
    return
  }
  if (event.origin !== hostOrigin || message.type !== 'response' || message.nonce !== nonce) return
  const request = pending.get(message.requestId)
  if (!request) return
  clearTimeout(request.timeout)
  pending.delete(message.requestId)
  if (message.ok === true) {
    request.resolve(message.result)
  } else {
    const error = new Error(message.error?.code || 'REQUEST_REJECTED') as Error & { code?: string }
    error.code = message.error?.code || 'REQUEST_REJECTED'
    request.reject(error)
  }
}

window.addEventListener('message', receive)

const send = async (capability: string, payload?: unknown) => {
  await ready
  if (!hostOrigin || !nonce) throw Object.assign(new Error('HOST_NOT_BOUND'), { code: 'HOST_NOT_BOUND' })
  const requestId = `req-${++sequence}`
  return new Promise((resolve, reject) => {
    const timeout = window.setTimeout(() => {
      pending.delete(requestId)
      reject(Object.assign(new Error('REQUEST_TIMEOUT'), { code: 'REQUEST_TIMEOUT' }))
    }, 1300)
    pending.set(requestId, { resolve, reject, timeout })
    window.parent.postMessage({ type: 'request', requestId, nonce, capability, payload }, hostOrigin)
  })
}

export function requestCapability<C extends SidecarCapability>(capability: C, payload?: unknown): Promise<unknown> {
  return send(capability, payload)
}

export function installFixtureDebug() {
  window.__fixtureDebug = {
    requestUnsafe: (capability: string, payload?: unknown) => send(capability, payload),
    sendRaw: (message: unknown) => window.parent.postMessage(message, hostOrigin)
  }
}
