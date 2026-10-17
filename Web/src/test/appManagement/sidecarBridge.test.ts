import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  SIDECAR_BRIDGE_PROTOCOL,
  type SidecarGrant,
  type SidecarGrantMessage,
  type SidecarRequestMessage,
} from '@/types/sidecar'
import {
  SidecarBroker,
  VideoSessionTracker,
  buildGrantMessage,
  buildRevokeMessage,
  isCrossSite,
  validateBridgeEnvelope,
  type BridgeServerCall,
} from '@/services/sidecarBridge'

const identity = {
  publisher_id: 'cnt-publisher',
  app_id: 'line-counting-ui',
  publisher_key_sha256: 'cc7bf1329f0bc349a467539027d0b826dee6e5ccf55afdf2be0d156a5a086d9c',
  native_tbs_sha256: '02d992d8dec446cf75b09e5c11e79b1908238cd5e22b186be443572f2ea1b242',
  ui_tbs_sha256: '28a1583f869c60fd480a7dba326ce6fc23b09a0071607ef8805521643e63bdb4',
  version_major: 1,
  version_minor: 4,
  version_patch: 2,
  host_ui_abi: 1,
  sidecar_service_generation: '7',
  host_boot_generation: 42,
  native_session_generation: 3,
  ui_generation: 11,
}

const grant: SidecarGrant = {
  grant_id: 'a'.repeat(64),
  expires_at_ms: 999999,
  issued_at_ms: 1,
  capabilities: ['snapshot.read', 'config.read', 'config.apply', 'counters.reset'],
  identity,
}

const flushAsync = async (): Promise<void> => {
  for (let i = 0; i < 20; i++) {
    await Promise.resolve()
  }
}

class FakePort {
  onmessage: ((event: MessageEvent) => void) | null = null
  onmessageerror: ((event: MessageEvent) => void) | null = null
  posted: unknown[] = []
  closed = false
  postMessage(message: unknown): void {
    this.posted.push(message)
  }
  close(): void {
    this.closed = true
  }
  deliver(data: unknown, origin = 'https://app.example.com'): void {
    this.onmessage?.({ origin, data } as MessageEvent)
  }
}

class FakeWindow {
  readonly postedTargets: Array<{ message: unknown; targetOrigin: string }> = []
  postMessage(message: unknown, targetOrigin: string, _transfer?: Transferable[]): void {
    this.postedTargets.push({ message, targetOrigin })
  }
}

interface BrokerHarness {
  broker: SidecarBroker
  hostPort: FakePort
  window: FakeWindow
  serverCall: BridgeServerCall
  results: unknown[]
  revocations: string[]
}

function startBroker(overrides?: { serverCall?: BridgeServerCall }): BrokerHarness {
  const hostPort = new FakePort()
  const fakeWindow = new FakeWindow()
  const results: unknown[] = []
  const revocations: string[] = []
  const serverCall = overrides?.serverCall ?? (vi.fn(async () => ({
    outcome: 'APPLIED' as const,
  })) as unknown as BridgeServerCall)
  class FakeChannel {
    port1: FakePort = hostPort
    port2: FakePort = new FakePort()
  }
  const broker = new SidecarBroker({
    appWindow: fakeWindow as unknown as Window,
    appOrigin: 'https://app.example.com',
    grant,
    serverCall,
    onResult: (r) => results.push(r),
    onRevoke: (reason) => revocations.push(reason),
  })
  ;(globalThis as { MessageChannel?: unknown }).MessageChannel = FakeChannel
  broker.start()
  const posted = fakeWindow.postedTargets[0]
  expect(posted).toBeTruthy()
  expect(posted?.targetOrigin).toBe('https://app.example.com')
  return {
    broker,
    hostPort,
    window: fakeWindow,
    serverCall,
    results,
    revocations,
  }
}

describe('sidecar bridge v1 envelope validation', () => {
  it('accepts a golden REQUEST envelope and rejects structural deviations', () => {
    const good: SidecarRequestMessage = {
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      seq: 1,
      grant_id: grant.grant_id,
      identity,
      request_id: 5,
      op: 'snapshot.read',
      payload: { line: { y: 1 } },
    }
    expect(validateBridgeEnvelope(good).ok).toBe(true)
    const mutations: Array<[string, unknown]> = [
      ['protocol', 'ne301.sidecar.bridge.v2'],
      ['type', 'RESULT'],
      ['seq', '1'],
      ['seq', -1],
      ['grant_id', 'short'],
      ['identity', { app_id: 'x' }],
      ['request_id', 0],
      ['op', 'admin.grant'],
      ['payload', 'not-an-object'],
      ['expected_config_generation', '6'],
      ['confirm_token', 5],
    ]
    for (const [key, value] of mutations) {
      const mutated = { ...good, [key]: value } as unknown as Record<string, unknown>
      const verdict = validateBridgeEnvelope(mutated)
      expect(verdict.ok, `${key}=${String(value)} should fail closed`).toBe(false)
    }
  })

  it('rejects oversized messages beyond 16 KiB', () => {
    const huge = {
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      seq: 1,
      grant_id: grant.grant_id,
      identity,
      request_id: 5,
      op: 'snapshot.read',
      payload: { blob: 'x'.repeat(20000) },
    }
    expect(validateBridgeEnvelope(huge).ok).toBe(false)
  })
})

describe('sidecar broker handshake', () => {
  let originalMessageChannel: unknown

  beforeEach(() => {
    originalMessageChannel = globalThis.MessageChannel
    vi.useFakeTimers()
  })

  afterEach(() => {
    ;(globalThis as { MessageChannel?: unknown }).MessageChannel = originalMessageChannel
    vi.useRealTimers()
  })

  it('sends HELLO with exact target origin and a transferred port, then GRANT on the port', async () => {
    const harness = startBroker()
    const hello = harness.window.postedTargets[0]?.message as Record<string, unknown>
    expect(hello.type).toBe('HELLO')
    expect(hello.protocol).toBe(SIDECAR_BRIDGE_PROTOCOL)
    expect(hello.identity).toEqual(identity)
    await vi.advanceTimersByTimeAsync(60)
    const grantMessage = harness.hostPort.posted[0] as Record<string, unknown>
    expect(grantMessage.type).toBe('GRANT')
    expect(grantMessage.grant_id).toBe(grant.grant_id)
    expect((grantMessage.grant as Record<string, unknown>).capabilities).toEqual(
      grant.capabilities,
    )
    harness.broker.revoke('TEST_END')
    expect(harness.revocations).toContain('TEST_END')
  })

  it('relays a REQUEST to the server, echoes RESULT with request_id/op/seq/identity', async () => {
    const harness = startBroker({
      serverCall: vi.fn(async () => ({
        outcome: 'APPLIED' as const,
        config_generation: 6,
        state_revision: 5,
      })),
    })
    harness.hostPort.deliver({
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      seq: 1,
      grant_id: grant.grant_id,
      identity,
      request_id: 9,
      op: 'config.apply',
      expected_config_generation: 6,
      payload: { line: { y: 5 } },
    })
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(harness.results.length).toBe(1)
    const result = harness.results[0] as Record<string, unknown>
    expect(result.type).toBe('RESULT')
    expect(result.request_id).toBe(9)
    expect(result.op).toBe('config.apply')
    expect(result.outcome).toBe('APPLIED')
    expect(result.identity).toEqual(identity)
    expect(harness.hostPort.posted.length).toBe(2)
    expect(harness.serverCall).toHaveBeenCalledWith('config.apply', expect.objectContaining({ request_id: 9 }))
    harness.broker.revoke('TEST_END')
  })

  it('revokes on cross-origin forgery with a non-matching event origin', () => {
    const harness = startBroker()
    harness.hostPort.deliver(
      {
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'REQUEST',
        seq: 1,
        grant_id: grant.grant_id,
        identity,
        request_id: 1,
        op: 'snapshot.read',
      },
      'https://attacker.example.com',
    )
    expect(harness.revocations).toEqual(['ORIGIN_MISMATCH'])
    expect(harness.serverCall).not.toHaveBeenCalled()
  })

  it('revokes on protocol mismatch, opaque-origin sandbox identity, foreign grant and identity drift', () => {
    const harness = startBroker()
    harness.hostPort.deliver({ protocol: 'ne301.sidecar.bridge.v2' }, 'https://app.example.com')
    expect(harness.revocations).toEqual(['PROTOCOL_MISMATCH'])
    harness.broker.revoke('TEST_END')
  })

  it('never accepts origin=null as an identity proof for sandboxed frames', () => {
    const harness = startBroker()
    harness.hostPort.deliver(
      {
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'REQUEST',
        seq: 1,
        grant_id: grant.grant_id,
        identity,
        request_id: 2,
        op: 'snapshot.read',
      },
      'null',
    )
    expect(harness.revocations).toEqual(['ORIGIN_MISMATCH'])
  })

  it('rejects ops outside the grant capability whitelist without contacting the server', () => {
    const harness = startBroker()
    harness.hostPort.deliver({
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      seq: 1,
      grant_id: grant.grant_id,
      identity,
      request_id: 3,
      op: 'enable.apply',
    })
    expect(harness.results.length).toBe(1)
    const result = harness.results[0] as Record<string, unknown>
    expect(result.outcome).toBe('REJECTED')
    expect(result.reason).toBe('OP_NOT_IN_GRANT')
    expect(harness.serverCall).not.toHaveBeenCalled()
    harness.broker.revoke('TEST_END')
  })

  it('rejects replayed request_id values', async () => {
    const harness = startBroker()
    const request = {
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      grant_id: grant.grant_id,
      identity,
      op: 'snapshot.read',
    }
    harness.hostPort.deliver({ ...request, seq: 1, request_id: 4 })
    await flushAsync()
    harness.hostPort.deliver({ ...request, seq: 2, request_id: 4 })
    const outcomes = harness.results.map((r) => (r as Record<string, unknown>).outcome)
    expect(outcomes).toEqual(['APPLIED', 'REJECTED'])
    harness.broker.revoke('TEST_END')
  })

  it('bounds concurrent pending requests at 4 and rejects the fifth', () => {
    const harness = startBroker({
      serverCall: (_op, request) => new Promise((resolve) => {
        void request
        setTimeout(() => resolve({ outcome: 'APPLIED' as const }), 5000)
      }),
    })
    for (let i = 1; i <= 5; i++) {
      harness.hostPort.deliver({
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'REQUEST',
        seq: i,
        grant_id: grant.grant_id,
        identity,
        request_id: i,
        op: 'snapshot.read',
      })
    }
    const outcomes = harness.results.map((r) => (r as Record<string, unknown>).outcome)
    expect(outcomes.filter((o) => o === 'REJECTED')).toHaveLength(1)
    expect(harness.broker.pendingCount()).toBe(4)
    harness.broker.revoke('TEST_END')
  })

  it('times out a pending request to UNKNOWN without optimistic commit and revokes after repeated timeouts', async () => {
    const harness = startBroker({
      serverCall: (_op, request) => new Promise((resolve) => {
        void request
        setTimeout(() => resolve({ outcome: 'APPLIED' as const }), 60000)
      }),
    })
    harness.hostPort.deliver({
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      seq: 1,
      grant_id: grant.grant_id,
      identity,
      request_id: 10,
      op: 'config.apply',
      expected_config_generation: 6,
      payload: { a: 1 },
    })
    await vi.advanceTimersByTimeAsync(10001)
    const result = harness.results[0] as Record<string, unknown>
    expect(result.outcome).toBe('UNKNOWN')
    expect(result.reason).toBe('ACK_TIMEOUT')
    harness.hostPort.deliver({
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      seq: 2,
      grant_id: grant.grant_id,
      identity,
      request_id: 11,
      op: 'config.apply',
      expected_config_generation: 6,
      payload: { a: 2 },
    })
    await vi.advanceTimersByTimeAsync(10001)
    expect(harness.revocations).toContain('ACK_TIMEOUT_LIMIT')
    expect(harness.broker.isRevoked()).toBe(true)
    expect(harness.hostPort.closed).toBe(true)
  })

  it('treats a revoked server response as terminal revocation', async () => {
    const harness = startBroker({
      serverCall: vi.fn(async () => ({
        outcome: 'UNKNOWN' as const,
        reason: 'IDENTITY_OR_GENERATION_CHANGED',
        revoked: true,
      })),
    })
    harness.hostPort.deliver({
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'REQUEST',
      seq: 1,
      grant_id: grant.grant_id,
      identity,
      request_id: 20,
      op: 'snapshot.read',
    })
    await flushAsync()
    expect(harness.broker.isRevoked()).toBe(true)
    expect(harness.revocations).toEqual(['IDENTITY_OR_GENERATION_CHANGED'])
  })

  it('delivers dangerous-operation confirm tokens as typed EVENT with payload', async () => {
    const harness = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    const delivered = await harness.broker.deliverDangerousConfirm(
      'counters.reset',
      vi.fn(async () => 'token-123'),
    )
    expect(delivered).toBe(true)
    const event = harness.hostPort.posted[1] as Record<string, unknown>
    expect(event.type).toBe('EVENT')
    expect(event.event).toBe('dangerous.confirm')
    expect((event.payload as Record<string, unknown>).confirm_token).toBe('token-123')
    harness.broker.revoke('TEST_END')
  })
})

describe('video session tracker', () => {
  it('moves through Unavailable to Reconnecting to Live and back and notifies subscribers', () => {
    const tracker = new VideoSessionTracker()
    const seen: string[] = []
    tracker.subscribe((state) => seen.push(state))
    expect(seen).toEqual(['Unavailable'])
    tracker.markReconnecting()
    tracker.markLive()
    tracker.markUnavailable()
    tracker.markUnavailable()
    expect(seen).toEqual(['Unavailable', 'Reconnecting', 'Live', 'Unavailable'])
  })
})

describe('bridge message builders and site gate', () => {
  it('builds GRANT and REVOKE envelopes carrying protocol, seq, grant_id and identity', () => {
    const grantMessage = buildGrantMessage(grant, 7)
    expect(grantMessage.protocol).toBe(SIDECAR_BRIDGE_PROTOCOL)
    expect(grantMessage.seq).toBe(7)
    expect(grantMessage.grant_id).toBe(grant.grant_id)
    expect((grantMessage as SidecarGrantMessage).grant.capabilities).toEqual(grant.capabilities)
    const revoke = buildRevokeMessage(grant, 8, 'GENERATION_CHANGED')
    expect(revoke.type).toBe('REVOKE')
    expect(revoke.reason).toBe('GENERATION_CHANGED')
  })

  it('classifies cross-origin and cross-site pairs and refuses same-site substitutes', () => {
    expect(isCrossSite('http://127.0.0.1:8080', 'http://localhost:9090')).toBe(true)
    expect(isCrossSite('https://admin.ne301.local', 'https://app.ne301.local')).toBe(false)
    expect(isCrossSite('http://admin.example.com:8080', 'http://admin.example.com:9090')).toBe(false)
    expect(isCrossSite('http://127.0.0.1:8080', 'http://127.0.0.1:9090')).toBe(false)
    expect(isCrossSite('http://127.0.0.1:8080', 'http://127.0.0.1:8080')).toBe(false)
    expect(isCrossSite('not-a-url', 'http://localhost')).toBe(false)
  })
})
