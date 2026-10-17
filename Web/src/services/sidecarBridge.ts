import {
  SIDECAR_BRIDGE_PROTOCOL,
  SIDECAR_MAX_PENDING_REQUESTS,
  SIDECAR_MSG_MAX_BYTES,
  SIDECAR_OPS,
  SIDECAR_REQUEST_TIMEOUT_MS,
  SIDECAR_MAX_CONSECUTIVE_TIMEOUTS,
  type SidecarAppToHostMessage,
  type SidecarBridgeMessage,
  type SidecarGrant,
  type SidecarHostToAppMessage,
  type SidecarIdentity,
  type SidecarOp,
  type SidecarOutcome,
  type SidecarRequestMessage,
  type SidecarResultMessage,
  type SidecarRevokeMessage,
} from '@/types/sidecar'

const SHA256_HEX = /^[a-f0-9]{64}$/

const isPlainObject = (value: unknown): value is Record<string, unknown> =>
  typeof value === 'object' && value !== null && !Array.isArray(value)

const isUint32 = (value: unknown): value is number =>
  typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xffffffff

const identityValid = (identity: unknown): identity is SidecarIdentity => {
  if (!isPlainObject(identity)) return false
  const strings: Array<keyof SidecarIdentity> = [
    'publisher_id',
    'app_id',
    'publisher_key_sha256',
    'native_tbs_sha256',
    'ui_tbs_sha256',
    'sidecar_service_generation',
  ]
  for (const key of strings) {
    if (typeof identity[key] !== 'string' || (identity[key] as string).length === 0) return false
  }
  for (const key of ['publisher_key_sha256', 'native_tbs_sha256', 'ui_tbs_sha256'] as const) {
    if (!SHA256_HEX.test(identity[key] as string)) return false
  }
  for (const key of ['version_major', 'version_minor', 'version_patch', 'host_ui_abi'] as const) {
    if (!isUint32(identity[key])) return false
  }
  for (
    const key of [
      'host_boot_generation',
      'native_session_generation',
      'ui_generation',
    ] as const
  ) {
    if (!isUint32(identity[key])) return false
  }
  return true
}

export interface EnvelopeValidation {
  ok: boolean
  reason?: string
  message?: SidecarBridgeMessage
}

export function validateBridgeEnvelope(raw: unknown): EnvelopeValidation {
  const encoded = (() => {
    try {
      return JSON.stringify(raw)
    } catch {
      return null
    }
  })()
  if (encoded === null || encoded.length > SIDECAR_MSG_MAX_BYTES) {
    return { ok: false, reason: 'MESSAGE_SIZE_EXCEEDED' }
  }
  if (!isPlainObject(raw)) return { ok: false, reason: 'ENVELOPE_NOT_OBJECT' }
  if (raw.protocol !== SIDECAR_BRIDGE_PROTOCOL) return { ok: false, reason: 'PROTOCOL_MISMATCH' }
  if (typeof raw.type !== 'string') return { ok: false, reason: 'TYPE_MISSING' }
  if (!isUint32(raw.seq)) return { ok: false, reason: 'SEQ_INVALID' }
  if (typeof raw.grant_id !== 'string' || raw.grant_id.length !== 64) {
    return { ok: false, reason: 'GRANT_ID_INVALID' }
  }
  if (!identityValid(raw.identity)) return { ok: false, reason: 'IDENTITY_INVALID' }
  const type = raw.type
  if (type === 'REQUEST') {
    if (!isUint32(raw.request_id) || raw.request_id === 0) {
      return { ok: false, reason: 'REQUEST_ID_INVALID' }
    }
    if (typeof raw.op !== 'string' || !(SIDECAR_OPS as readonly string[]).includes(raw.op)) {
      return { ok: false, reason: 'OP_UNKNOWN' }
    }
    if (raw.payload !== undefined && !isPlainObject(raw.payload)) {
      return { ok: false, reason: 'PAYLOAD_NOT_OBJECT' }
    }
    if (
      raw.expected_config_generation !== undefined &&
      !isUint32(raw.expected_config_generation)
    ) {
      return { ok: false, reason: 'CONFIG_GENERATION_INVALID' }
    }
    if (raw.confirm_token !== undefined && typeof raw.confirm_token !== 'string') {
      return { ok: false, reason: 'CONFIRM_TOKEN_INVALID' }
    }
  } else if (type === 'REVOKE') {
    if (typeof raw.reason !== 'string' || raw.reason.length === 0) {
      return { ok: false, reason: 'REVOKE_REASON_MISSING' }
    }
  } else {
    return { ok: false, reason: 'TYPE_NOT_ALLOWED_FROM_APP' }
  }
  return { ok: true, message: raw as unknown as SidecarBridgeMessage }
}

export function buildGrantMessage(grant: SidecarGrant, seq: number): SidecarHostToAppMessage {
  return {
    protocol: SIDECAR_BRIDGE_PROTOCOL,
    type: 'GRANT',
    seq,
    grant_id: grant.grant_id,
    identity: grant.identity,
    grant: {
      grant_id: grant.grant_id,
      expires_at_ms: grant.expires_at_ms,
      capabilities: [...grant.capabilities],
    },
  }
}

export function buildRevokeMessage(
  grant: SidecarGrant,
  seq: number,
  reason: string,
): SidecarRevokeMessage {
  return {
    protocol: SIDECAR_BRIDGE_PROTOCOL,
    type: 'REVOKE',
    seq,
    grant_id: grant.grant_id,
    identity: grant.identity,
    reason,
  }
}

export interface BridgeServerCall {
  (op: SidecarOp, request: {
    grant_id: string
    request_id: number
    payload?: unknown
    expected_config_generation?: number
    confirm_token?: string
  }): Promise<{
    outcome: SidecarOutcome
    reason?: string
    freshness?: string
    config_generation?: number
    state_revision?: number
    age_ms?: number
    payload?: unknown
    revoked?: boolean
  }>
}

export interface SidecarBrokerOptions {
  appWindow: Window
  appOrigin: string
  grant: SidecarGrant
  serverCall: BridgeServerCall
  onResult?: (result: SidecarResultMessage) => void
  onRevoke?: (reason: string) => void
  timeoutMs?: number
}

export type VideoSessionState = 'Unavailable' | 'Reconnecting' | 'Live'

export class VideoSessionTracker {
  private state: VideoSessionState = 'Unavailable'
  private listeners = new Set<(state: VideoSessionState) => void>()

  current(): VideoSessionState {
    return this.state
  }

  subscribe(listener: (state: VideoSessionState) => void): () => void {
    this.listeners.add(listener)
    listener(this.state)
    return () => {
      this.listeners.delete(listener)
    }
  }

  markUnavailable(): void {
    this.set('Unavailable')
  }

  markReconnecting(): void {
    this.set('Reconnecting')
  }

  markLive(): void {
    this.set('Live')
  }

  private set(state: VideoSessionState): void {
    if (this.state === state) return
    this.state = state
    for (const listener of this.listeners) listener(state)
  }
}

interface PendingRequest {
  op: SidecarOp
  seq: number
  timer: ReturnType<typeof setTimeout>
  resolve: (result: SidecarResultMessage) => void
}

export class SidecarBroker {
  readonly grant: SidecarGrant

  readonly video = new VideoSessionTracker()

  private readonly appWindow: Window

  private readonly appOrigin: string

  private readonly serverCall: BridgeServerCall

  private readonly onResult?: (result: SidecarResultMessage) => void

  private readonly onRevoke?: (reason: string) => void

  private readonly timeoutMs: number

  private channel: MessageChannel | null = null

  private inboundSeq = 0

  private outboundSeq = 0

  private readonly seenRequestIds = new Set<number>()

  private readonly pending = new Map<number, PendingRequest>()

  private consecutiveTimeouts = 0

  private revoked = false

  private started = false

  constructor(options: SidecarBrokerOptions) {
    this.appWindow = options.appWindow
    this.appOrigin = options.appOrigin
    this.grant = options.grant
    this.serverCall = options.serverCall
    this.onResult = options.onResult
    this.onRevoke = options.onRevoke
    this.timeoutMs = options.timeoutMs ?? SIDECAR_REQUEST_TIMEOUT_MS
  }

  start(): void {
    if (this.started || this.revoked) return
    this.started = true
    const channel = new MessageChannel()
    this.channel = channel
    channel.port1.onmessage = (event: MessageEvent) => this.handlePortMessage(event)
    channel.port1.onmessageerror = () => this.revoke('MESSAGE_DECODE_ERROR')
    const hello = {
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'HELLO',
      seq: ++this.outboundSeq,
      grant_id: this.grant.grant_id,
      identity: this.grant.identity,
      channel: SIDECAR_BRIDGE_PROTOCOL,
    }
    this.appWindow.postMessage(hello, this.appOrigin, [channel.port2])
    setTimeout(() => {
      if (this.revoked || this.channel === null) return
      try {
        this.post(buildGrantMessage(this.grant, ++this.outboundSeq))
      } catch {
        void 0
      }
    }, 0)
  }

  revoke(reason: string): void {
    if (this.revoked) return
    this.revoked = true
    if (this.channel !== null) {
      try {
        this.post(buildRevokeMessage(this.grant, ++this.outboundSeq, reason))
      } catch {
        void 0
      }
      this.channel.port1.onmessage = null
      this.channel.port1.onmessageerror = null
      try {
        this.channel.port1.close()
      } catch {
        void 0
      }
      this.channel = null
    }
    for (const [, entry] of this.pending) {
      clearTimeout(entry.timer)
      entry.resolve(this.makeResult(entry.op, entry.seq, 'UNKNOWN', 'GRANT_REVOKED'))
    }
    this.pending.clear()
    this.video.markUnavailable()
    this.onRevoke?.(reason)
  }

  isRevoked(): boolean {
    return this.revoked
  }

  pendingCount(): number {
    return this.pending.size
  }

  async deliverDangerousConfirm(
    op: SidecarOp,
    requestConfirm: (grantId: string, op: SidecarOp) => Promise<string>,
  ): Promise<boolean> {
    if (this.revoked) return false
    let token: string
    try {
      token = await requestConfirm(this.grant.grant_id, op)
    } catch {
      return false
    }
    try {
      this.post({
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'EVENT',
        seq: ++this.outboundSeq,
        grant_id: this.grant.grant_id,
        identity: this.grant.identity,
        event: 'dangerous.confirm',
        payload: { op, confirm_token: token },
      })
    } catch {
      void 0
    }
    return true
  }

  private post(message: SidecarHostToAppMessage): void {
    if (this.channel === null) throw new Error('BRIDGE_PORT_CLOSED')
    const encoded = JSON.stringify(message)
    if (encoded.length > SIDECAR_MSG_MAX_BYTES) throw new Error('MESSAGE_SIZE_EXCEEDED')
    this.channel.port1.postMessage(message)
  }

  private makeResult(
    op: SidecarOp,
    seq: number,
    outcome: SidecarOutcome,
    reason?: string,
    extra?: Partial<SidecarResultMessage>,
  ): SidecarResultMessage {
    return {
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'RESULT',
      seq,
      grant_id: this.grant.grant_id,
      identity: this.grant.identity,
      request_id: 0,
      op,
      outcome,
      reason,
      ...extra,
    }
  }

  private async handlePortMessage(event: MessageEvent): Promise<void> {
    if (this.revoked) return
    if (event.origin !== this.appOrigin && event.origin !== '') {
      this.revoke('ORIGIN_MISMATCH')
      return
    }
    if (isPlainObject(event.data) && (event.data as Record<string, unknown>).probe === 'ready') {
      setTimeout(() => {
        if (this.revoked || this.channel === null) return
        try {
          this.post(buildGrantMessage(this.grant, ++this.outboundSeq))
        } catch {
          void 0
        }
      }, 50)
      return
    }
    const validation = validateBridgeEnvelope(event.data)
    if (!validation.ok || !validation.message) {
      this.revoke(validation.reason ?? 'ENVELOPE_INVALID')
      return
    }
    const message = validation.message as SidecarAppToHostMessage
    if (message.grant_id !== this.grant.grant_id) {
      this.revoke('GRANT_MISMATCH')
      return
    }
    if (JSON.stringify(message.identity) !== JSON.stringify(this.grant.identity)) {
      this.revoke('IDENTITY_MISMATCH')
      return
    }
    if (message.seq <= this.inboundSeq) {
      this.revoke('SEQ_REGRESSED')
      return
    }
    this.inboundSeq = message.seq
    if (message.type === 'REVOKE') {
      this.revoke('APP_REQUESTED_REVOKE')
      return
    }
    const request = message as SidecarRequestMessage
    if (this.pending.size >= SIDECAR_MAX_PENDING_REQUESTS) {
      this.postResult(request, 'REJECTED', { reason: 'TOO_MANY_PENDING' })
      return
    }
    if (this.seenRequestIds.has(request.request_id)) {
      this.postResult(request, 'REJECTED', { reason: 'REPLAY' })
      return
    }
    if (!(this.grant.capabilities as readonly string[]).includes(request.op)) {
      this.postResult(request, 'REJECTED', { reason: 'OP_NOT_IN_GRANT' })
      return
    }
    this.seenRequestIds.add(request.request_id)
    if (this.seenRequestIds.size > 256) {
      const oldest = this.seenRequestIds.values().next().value
      if (oldest !== undefined) this.seenRequestIds.delete(oldest)
    }
    const seq = ++this.outboundSeq
    const timer = setTimeout(() => {
      const pending = this.pending.get(request.request_id)
      if (!pending) return
      this.pending.delete(request.request_id)
      this.consecutiveTimeouts += 1
      if (this.consecutiveTimeouts >= SIDECAR_MAX_CONSECUTIVE_TIMEOUTS) {
        this.revoke('ACK_TIMEOUT_LIMIT')
        return
      }
      this.postResult(request, 'UNKNOWN', { reason: 'ACK_TIMEOUT' })
    }, this.timeoutMs)
    this.pending.set(request.request_id, {
      op: request.op,
      seq,
      timer,
      resolve: (result) => {
        this.onResult?.(result)
        try {
          this.post(result)
        } catch {
          void 0
        }
      },
    })
    try {
      const response = await this.serverCall(request.op, {
        grant_id: this.grant.grant_id,
        request_id: request.request_id,
        payload: request.payload,
        expected_config_generation: request.expected_config_generation,
        confirm_token: request.confirm_token,
      })
      const pending = this.pending.get(request.request_id)
      if (!pending) return
      this.pending.delete(request.request_id)
      clearTimeout(pending.timer)
      this.consecutiveTimeouts = 0
      if (response.revoked) {
        this.revoke(response.reason ?? 'SERVER_REVOKED')
        return
      }
      this.postResult(request, response.outcome, {
        reason: response.reason,
        freshness: response.freshness,
        config_generation: response.config_generation,
        state_revision: response.state_revision,
        age_ms: response.age_ms,
        payload: response.payload,
        echoRequestId: request.request_id,
      })
    } catch {
      const pending = this.pending.get(request.request_id)
      if (!pending) return
      this.pending.delete(request.request_id)
      clearTimeout(pending.timer)
      this.postResult(request, 'UNKNOWN', { reason: 'TRANSPORT_FAILED' })
    }
  }

  private postResult(
    request: SidecarRequestMessage,
    outcome: SidecarOutcome,
    extra: {
      reason?: string
      freshness?: string
      config_generation?: number
      state_revision?: number
      age_ms?: number
      payload?: unknown
      echoRequestId?: number
    },
  ): void {
    const pending = this.pending.get(request.request_id)
    const seq = pending ? pending.seq : ++this.outboundSeq
    const result = this.makeResult(request.op, seq, outcome, extra.reason, {
      request_id: extra.echoRequestId ?? request.request_id,
      freshness: extra.freshness,
      config_generation: extra.config_generation,
      state_revision: extra.state_revision,
      age_ms: extra.age_ms,
      payload: extra.payload,
    })
    this.onResult?.(result)
    try {
      this.post(result)
    } catch {
      void 0
    }
  }
}

export function isCrossSite(originA: string, originB: string): boolean {
  if (!originA || !originB) return false
  let a: URL
  let b: URL
  try {
    a = new URL(originA)
    b = new URL(originB)
  } catch {
    return false
  }
  if (a.protocol !== 'http:' && a.protocol !== 'https:') return false
  if (b.protocol !== 'http:' && b.protocol !== 'https:') return false
  if (a.origin === b.origin) return false
  const siteOf = (url: URL): string => {
    const host = url.hostname.toLowerCase()
    if (
      /^(\d{1,3}\.){3}\d{1,3}$/.test(host) ||
      host.startsWith('[') ||
      host === 'localhost'
    ) {
      return host
    }
    const labels = host.split('.')
    if (labels.length <= 2) return labels.join('.')
    return labels.slice(-2).join('.')
  }
  return siteOf(a) !== siteOf(b)
}
