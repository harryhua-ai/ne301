import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent, cleanup } from '@testing-library/preact'
import { I18nWrapper, setLocale } from '@/i18n'
import LineCrossingPage from '@/pages/applicationManagement/linecrossing/LineCrossingPage'
import { useAppSessionStore } from '@/store/appSession'
import {
  validateLineConfigCandidate,
  encodeLineConfigCandidate,
  candidateToBase64,
  isV3LineCrossingCapable,
  LC_CONFIG_CANDIDATE_SIZE,
  LC_SNAPSHOT_SCHEMA,
  type BusinessSnapshotResponse,
  type AppSessionStatusResponse,
  type LineCrossingSnapshotPayload,
} from '@/services/api/appSession'
import { requestMock, setupSuite, teardownSuite, envelopeOk } from './helpers'

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
}))

vi.mock('@/pages/applicationManagement/linecrossing/VideoPreview', () => ({
  default: ({ config, editMode, editPhase, onPickPoint }: {
    config: {
      line_x1_permille: number
      line_y1_permille: number
      line_x2_permille: number
      line_y2_permille: number
      outside_x_permille: number
      outside_y_permille: number
    } | null
    editMode: boolean
    editPhase: number
    onPickPoint: (x: number, y: number) => void
  }) => (
    <div
      data-testid="lc-video-stub"
      data-edit-mode={String(editMode)}
      data-edit-phase={String(editPhase)}
      data-line={config
        ? [
            config.line_x1_permille,
            config.line_y1_permille,
            config.line_x2_permille,
            config.line_y2_permille,
            config.outside_x_permille,
            config.outside_y_permille,
          ].join(',')
        : ''}
    >
      <button data-testid="pc-stage-pick-first" onClick={() => onPickPoint(200, 300)}>first</button>
      <button data-testid="pc-stage-pick-second" onClick={() => onPickPoint(700, 800)}>second</button>
    </div>
  ),
}))

const activeConfig = {
  counter_name: 'door',
  target_class_name: 'person',
  line: { x1_permille: 700, y1_permille: 100, x2_permille: 700, y2_permille: 900 },
  outside: { x_permille: 800, y_permille: 500 },
  conf_threshold_permille: 500,
  max_dist_permille: 100,
  track_history_k: 8,
  max_miss: 5,
  k_confirm: 3,
  window_minutes: 30,
  tracks_report_enable: false,
  heat_grid_enable: false,
}

const snapshotPayload = (overrides: Record<string, unknown> = {}) => ({
    schema: LC_SNAPSHOT_SCHEMA,
    config_generation: 7,
    state_revision: 42,
    app_state: 'running',
    storage: { state: 'ok', withheld: false },
    report_seq: 3,
    cumulative: { in: 11, out: 4 },
    counters: { in: 2, out: 1 },
    gap: { gaps: 1, lost: 0, lost_unknown: 0, unusable: 0 },
    model: { valid: true, generation: 2, class_generation: 5, target_class: 'person' },
    active_config: activeConfig,
    ...overrides,
  }) as unknown as LineCrossingSnapshotPayload

const snapshotEnvelope = (payload: unknown, overrides: Record<string, unknown> = {}) => envelopeOk({
    availability: 'fresh',
    session_generation: 1,
    visible_version: 1,
    config_generation: 7,
    state_revision: 42,
    age_ms: 1000,
    payload_len: 500,
    publisher_id: 'pub-alpha',
    app_id: 'hello-app',
    payload,
    ...overrides,
  }) as { data: BusinessSnapshotResponse }

const runningV3Session = () => ({
    state: 'running',
    publisher_id: 'pub-alpha',
    app_id: 'hello-app',
    app_running: true,
    abi_version: 196608,
    granted_caps: '0x000000c8',
  }) as AppSessionStatusResponse

const reportsEnvelope = (bound: boolean, reports: unknown[] = []) => envelopeOk({
    session_bound: bound,
    session_state: bound ? 'running' : 'idle',
    host_boot_id: bound ? 5746703 : 0,
    queue_slots: 8,
    queue_used: reports.length,
    reports,
  })

const resetSessionStore = () => {
  useAppSessionStore.setState({
    session: null,
    sessionAuthBlocked: null,
    snapshot: null,
    snapshotPayload: null,
    reports: null,
    control: null,
    busy: false,
    outcome: null,
    watch: { phase: 'idle' },
    capabilityAppId: null,
  })
}

beforeEach(() => {
  setupSuite()
  resetSessionStore()
})

afterEach(() => {
  teardownSuite()
})

describe('line-crossing v3 capability gate', () => {
  it('accepts only a running v3 session with report/control/snapshot caps and an app-owned snapshot', () => {
    const snap = snapshotEnvelope(snapshotPayload())
    expect(isV3LineCrossingCapable(runningV3Session(), snap.data)).toBe(true)
    expect(isV3LineCrossingCapable({ ...runningV3Session(), state: 'idle' }, snap.data)).toBe(false)
    expect(
      isV3LineCrossingCapable({ ...runningV3Session(), abi_version: 0x00020000 }, snap.data),
    ).toBe(false)
    expect(
      isV3LineCrossingCapable({ ...runningV3Session(), granted_caps: '0x00000008' }, snap.data),
    ).toBe(false)
    const wrongSchema = snapshotEnvelope({ schema: 'line-crossing/2' })
    expect(isV3LineCrossingCapable(runningV3Session(), wrongSchema.data)).toBe(false)
    expect(isV3LineCrossingCapable(runningV3Session(), undefined)).toBe(false)
  })
})

describe('config candidate codec (App-owned schema mirrored, never invented)', () => {
  const valid = {
    counter_name: 'door',
    target_class_name: 'person',
    line: { x1: 700, y1: 100, x2: 700, y2: 900 },
    outside: { x: 800, y: 500 },
    conf_threshold_permille: 500,
    max_dist_permille: 100,
    track_history_k: 8,
    max_miss: 5,
    k_confirm: 3,
    window_minutes: 30,
    tracks_report_enable: false,
    heat_grid_enable: false,
  }

  it('encodes a 144-byte candidate with the LCC3 magic and schema 1', () => {
    expect(validateLineConfigCandidate(valid).ok).toBe(true)
    const bytes = encodeLineConfigCandidate(valid)
    expect(bytes.length).toBe(LC_CONFIG_CANDIDATE_SIZE)
    expect([bytes[0], bytes[1], bytes[2], bytes[3]]).toEqual([0x4c, 0x43, 0x43, 0x33])
    expect(candidateToBase64(bytes)).toHaveLength(192)
    const dv = new DataView(bytes.buffer)
    expect(dv.getUint32(4, true)).toBe(1)
    expect(dv.getUint32(8, true)).toBe(144)
    expect(dv.getUint32(12, true)).toBe(0)
    expect(bytes[16]).toBe('person'.length)
    expect(bytes[49]).toBe('door'.length)
    expect(dv.getUint16(134, true)).toBe(30)
  })

  it('rejects invalid drafts field by field', () => {
    expect(validateLineConfigCandidate({ ...valid, counter_name: '' }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, target_class_name: 'a'.repeat(32) }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, line: { x1: 5, y1: 5, x2: 5, y2: 5 } }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, conf_threshold_permille: 1001 }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, max_dist_permille: 0 }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, track_history_k: 3 }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, max_miss: 0 }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, window_minutes: 1441 }).ok).toBe(false)
    expect(validateLineConfigCandidate({ ...valid, outside: { x: -1, y: 0 } }).ok).toBe(false)
  })
})

describe('line crossing business page (isolated web, real API shapes)', () => {
  const stubRunning = (snapEnvelope: unknown, reportEnv: unknown, controlEnv: unknown) => {
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/business/snapshot')) return Promise.resolve(snapEnvelope)
      if (u.includes('/api/v1/app/report/status')) return Promise.resolve(reportEnv)
      if (u.includes('/api/v1/app/control/status')) return Promise.resolve(controlEnv)
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
  }

  it('keeps the inherited grouped editor and two-point line workflow in the business page', async () => {
    stubRunning(
      snapshotEnvelope(snapshotPayload()),
      reportsEnvelope(true, []),
      envelopeOk({ session_active: true, command: { state: 'none' } }),
    )
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 4096,
            packageSha256: 'ee55',
            tbsSha256: 'ff66',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect(screen.getByTestId('pc-source-config')).toBeTruthy()
    })
    expect(screen.getByTestId('pc-group-line')).toBeTruthy()
    expect(screen.getByTestId('pc-group-detection')).toBeTruthy()
    expect(screen.getByTestId('pc-group-tracking')).toBeTruthy()
    expect(screen.getByTestId('pc-group-reporting')).toBeTruthy()
    expect(screen.getByTestId('lc-reports')).toBeTruthy()
    fireEvent.click(screen.getByTestId('pc-edit-line'))
    expect(screen.getByTestId('lc-video-stub').getAttribute('data-edit-mode')).toBe('true')
    fireEvent.click(screen.getByTestId('pc-stage-pick-first'))
    expect(screen.getByTestId('lc-video-stub').getAttribute('data-edit-phase')).toBe('1')
    fireEvent.click(screen.getByTestId('pc-stage-pick-second'))
    expect(screen.getByTestId('lc-video-stub').getAttribute('data-edit-phase')).toBe('2')
    expect(screen.getByTestId('lc-video-stub').getAttribute('data-line')).toBe('200,300,700,800,344,656')
    fireEvent.click(screen.getByTestId('pc-flip-direction'))
    expect(screen.getByTestId('lc-video-stub').getAttribute('data-line')).toBe('200,300,700,800,556,444')
  })

  it('shows snapshot counters, freshness annotation, and truthful report channel states', async () => {
    stubRunning(
      snapshotEnvelope(snapshotPayload()),
      reportsEnvelope(true, [
        { report_seq: 5, mqtt_state: 'succeeded', webhook_state: 'pending', mqtt_attempts: 1, webhook_attempts: 0, payload_len: 400 },
        { report_seq: 4, mqtt_state: 'failed', webhook_state: 'not_configured', mqtt_attempts: 3, webhook_attempts: 0, payload_len: 380 },
      ]),
      envelopeOk({ session_active: true }),
    )
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 4096,
            packageSha256: 'ee55',
            tbsSha256: 'ff66',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect(useAppSessionStore.getState().capabilityAppId).toBe('hello-app')
    })
    expect(screen.getByTestId('lc-cum-in').textContent).toBe('11')
    expect(screen.getByTestId('lc-cum-out').textContent).toBe('4')
    expect(screen.getByTestId('lc-freshness').textContent).toBe('Live')
    expect(screen.getByTestId('lc-report-0').textContent).toContain('Delivered to broker')
    expect(screen.getByTestId('lc-report-1').textContent).toContain('Not configured')
    expect(useAppSessionStore.getState().snapshotPayload?.report_seq).toBe(3)
  })

  it('marks stale snapshots (>10s) instead of passing them off as live', async () => {
    stubRunning(
      snapshotEnvelope(snapshotPayload(), { availability: 'stale', age_ms: 12000 }),
      reportsEnvelope(false, []),
      envelopeOk({ session_active: true }),
    )
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 1,
            packageSha256: 'x',
            tbsSha256: 'y',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect(screen.getByTestId('lc-snapshot-stale')).toBeTruthy()
    })
    expect(screen.getByTestId('lc-freshness').textContent).toBe('Stale')
  })

  it('does not leak reports from a previous session when the queue is unbound', async () => {
    stubRunning(
      snapshotEnvelope(snapshotPayload()),
      reportsEnvelope(false, []),
      envelopeOk({ session_active: true }),
    )
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 1,
            packageSha256: 'x',
            tbsSha256: 'y',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect(screen.getByTestId('lc-reports-unbound')).toBeTruthy()
    })
    expect(screen.queryByTestId('lc-report-0')).toBeNull()
  })

  it('saves hot config only after a matching receipt and never paints success without APPLIED', async () => {
    const posted: Record<string, unknown>[] = []
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/business/snapshot')) {
        return Promise.resolve(snapshotEnvelope(snapshotPayload()))
      }
      if (u.includes('/api/v1/app/report/status')) return Promise.resolve(reportsEnvelope(true, []))
      if (u.includes('/api/v1/app/control/status')) {
        return Promise.resolve(
          envelopeOk({
            session_active: true,
            session_generation: 1,
            command: {
              state: 'applied',
              request_id: posted[0]?.request_id,
              msg_type: 'CONFIG_APPLY',
              result_config_generation: 8,
              result_state_revision: 43,
            },
          }),
        )
      }
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
    requestMock.post.mockImplementation((url: string, body?: unknown) => {
      if (String(url).includes('/api/v1/app/control/config')) {
        posted.push(body as Record<string, unknown>)
        return Promise.resolve(envelopeOk({ status: 'registered', command: { state: 'pending' } }))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 1,
            packageSha256: 'x',
            tbsSha256: 'y',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect((screen.getByTestId('lc-save') as unknown as HTMLButtonElement).disabled).toBe(false)
    })
    fireEvent.click(screen.getByTestId('lc-save'))
    await waitFor(() => {
      expect(screen.getByTestId('lc-watch-applied')).toBeTruthy()
    }, { timeout: 4000 })
    expect(posted).toHaveLength(1)
    expect(posted[0].expected_config_generation).toBe(7)
    expect((posted[0].payload_base64 as string).length).toBe(192)
    expect(useAppSessionStore.getState().watch.phase).toBe('terminal')
    expect((useAppSessionStore.getState().watch as { cmdState?: string }).cmdState).toBe('applied')
  }, 15000)

  it('requires explicit admin confirmation before a target-class change (destructive) save', async () => {
    stubRunning(
      snapshotEnvelope(snapshotPayload()),
      reportsEnvelope(true, []),
      envelopeOk({ session_active: true, command: { state: 'none' } }),
    )
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 1,
            packageSha256: 'x',
            tbsSha256: 'y',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect(useAppSessionStore.getState().snapshotPayload?.config_generation).toBe(7)
    })
    const targetInput = await screen.findByTestId('lc-target-class')
    fireEvent.input(targetInput, { target: { value: 'vehicle' } })
    await waitFor(() => {
      expect(screen.getByTestId('lc-destructive-ack')).toBeTruthy()
    })
    const save = screen.getByTestId('lc-save') as HTMLButtonElement
    expect(save.disabled).toBe(true)
    fireEvent.click(screen.getByTestId('lc-destructive-checkbox'))
    expect((screen.getByTestId('lc-destructive-checkbox') as HTMLInputElement).checked).toBe(true)
    expect(save.disabled).toBe(false)
  })

  it('shows a stale-config rejection truthfully and never marks it applied', async () => {
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/business/snapshot')) {
        return Promise.resolve(snapshotEnvelope(snapshotPayload()))
      }
      if (u.includes('/api/v1/app/report/status')) return Promise.resolve(reportsEnvelope(true, []))
      if (u.includes('/api/v1/app/control/status')) {
        return Promise.resolve(
          envelopeOk({
            session_active: true,
            command: {
              state: 'rejected_stale_config',
              request_id: posted[0]?.request_id,
              msg_type: 'CONFIG_APPLY',
              result_config_generation: 7,
            },
          }),
        )
      }
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
    const posted: Record<string, unknown>[] = []
    requestMock.post.mockImplementation((url: string, body?: unknown) => {
      if (String(url).includes('/api/v1/app/control/config')) {
        posted.push(body as Record<string, unknown>)
        return Promise.resolve(envelopeOk({ status: 'registered' }))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 1,
            packageSha256: 'x',
            tbsSha256: 'y',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect((screen.getByTestId('lc-save') as unknown as HTMLButtonElement).disabled).toBe(false)
    })
    fireEvent.click(screen.getByTestId('lc-save'))
    await waitFor(() => {
      expect(screen.getByTestId('lc-watch-terminal')).toBeTruthy()
    }, { timeout: 4000 })
    expect(screen.getByTestId('lc-watch-terminal').textContent).toContain('stale')
    expect(screen.queryByTestId('lc-watch-applied')).toBeNull()
  }, 15000)

  it('reports an unconfirmed unknown result after wait timeout instead of success', async () => {
    vi.useFakeTimers()
    try {
      requestMock.get.mockImplementation((url: string) => {
        const u = String(url)
        if (u.includes('/api/v1/app/business/snapshot')) {
          return Promise.resolve(snapshotEnvelope(snapshotPayload()))
        }
        if (u.includes('/api/v1/app/report/status')) return Promise.resolve(reportsEnvelope(true, []))
        if (u.includes('/api/v1/app/control/status')) {
          return Promise.resolve(
            envelopeOk({ session_active: true, command: { state: 'pending', request_id: 99 } }),
          )
        }
        return Promise.reject(new Error(`unexpected GET ${u}`))
      })
      requestMock.post.mockImplementation((url: string) => {
        if (String(url).includes('/api/v1/app/control/config')) {
          return Promise.resolve(envelopeOk({ status: 'registered' }))
        }
        return Promise.reject(new Error(`unexpected POST ${url}`))
      })
      useAppSessionStore.setState({ session: runningV3Session() })
      const { unmount } = render(
        <I18nWrapper>
          <LineCrossingPage
            record={{
              publisherId: 'pub-alpha',
              appId: 'hello-app',
              version: 'v3.0.0',
              packageLen: 1,
              packageSha256: 'x',
              tbsSha256: 'y',
            }}
          />
        </I18nWrapper>,
      )
      await vi.advanceTimersByTimeAsync(300)
      expect(useAppSessionStore.getState().snapshotPayload?.config_generation).toBe(7)
      await vi.advanceTimersByTimeAsync(200)
      const saveBtn = screen.getByTestId('lc-save') as unknown as HTMLButtonElement
      expect(saveBtn.disabled).toBe(false)
      fireEvent.click(saveBtn)
      await vi.advanceTimersByTimeAsync(32000)
      expect(useAppSessionStore.getState().watch.phase).toBe('terminal')
      const watch = useAppSessionStore.getState().watch as { cmdState?: string; timedOut?: boolean }
      expect(watch.cmdState).toBe('unknown')
      expect(watch.timedOut).toBe(true)
      unmount()
    } finally {
      vi.useRealTimers()
    }
  }, 15000)

  it('opens the reset counters flow only behind an explicit confirmation', async () => {
    const posted: Record<string, unknown>[] = []
    stubRunning(
      snapshotEnvelope(snapshotPayload()),
      reportsEnvelope(true, []),
      envelopeOk({ session_active: true, command: { state: 'none' } }),
    )
    requestMock.post.mockImplementation((url: string, body?: unknown) => {
      if (String(url).includes('/api/v1/app/control/reset')) {
        posted.push(body as Record<string, unknown>)
        return Promise.resolve(envelopeOk({ status: 'registered' }))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 1,
            packageSha256: 'x',
            tbsSha256: 'y',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect(screen.getByTestId('lc-reset-open')).toBeTruthy()
    })
    fireEvent.click(screen.getByTestId('lc-reset-open'))
    expect(screen.getByTestId('lc-reset-confirm')).toBeTruthy()
    fireEvent.click(screen.getByTestId('lc-reset-confirm-btn'))
    await waitFor(() => {
      expect(posted).toHaveLength(1)
    })
    expect(posted[0].destructive_confirmed).toBe(true)
    expect(posted[0].expected_config_generation).toBe(7)
    await waitFor(() => {
      cleanup()
    })
  }, 40000)

  it('renders zh labels for stats and reports (bilingual regression)', async () => {
    setLocale('zh')
    stubRunning(
      snapshotEnvelope(snapshotPayload()),
      reportsEnvelope(true, [
        { report_seq: 2, mqtt_state: 'pending', webhook_state: 'succeeded', mqtt_attempts: 0, webhook_attempts: 1, payload_len: 300 },
      ]),
      envelopeOk({ session_active: true }),
    )
    useAppSessionStore.setState({ session: runningV3Session() })
    render(
      <I18nWrapper>
        <LineCrossingPage
          record={{
            publisherId: 'pub-alpha',
            appId: 'hello-app',
            version: 'v3.0.0',
            packageLen: 1,
            packageSha256: 'x',
            tbsSha256: 'y',
          }}
        />
      </I18nWrapper>,
    )
    await waitFor(() => {
      expect(screen.getByTestId('lc-cum-in').textContent).toBe('11')
    })
    expect(screen.getByTestId('lc-win-in').parentElement?.textContent).toContain('进')
    expect(screen.getByText('上报状态（当前会话报告队列，只读）')).toBeTruthy()
    expect(screen.getByTestId('lc-report-0').textContent).toContain('待发送')
    expect(screen.getByTestId('lc-report-0').textContent).toContain('成功')
    await waitFor(() => {
      expect(screen.getByTestId('pc-group-line')).toBeTruthy()
    })
    expect(screen.getByTestId('pc-group-line').textContent).toContain('计数线')
    expect(screen.getByTestId('pc-group-reporting').textContent).toContain('上报')
  })
})
