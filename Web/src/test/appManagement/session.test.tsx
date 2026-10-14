import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import { useAppSessionStore } from '@/store/appSession'
import {
  requestMock,
  installedState,
  stubGetInstalled,
  setupSuite,
  teardownSuite,
  envelopeOk,
  envelopeFail,
} from './helpers'

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
}))

vi.mock('sonner', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() },
}))

vi.mock('@/services/api/applicationManagement', () => ({
  default: {
    getMqttConfigReq: vi.fn(() => Promise.resolve({ data: {} })),
    setMqttConfigReq: vi.fn(),
    connectMqttReq: vi.fn(),
    disconnectMqttReq: vi.fn(),
    getWebhookConfigReq: vi.fn(() => Promise.resolve({ data: {} })),
    setWebhookConfigReq: vi.fn(),
    testWebhookReq: vi.fn(),
    getWebhookCaCertReq: vi.fn(),
    setWebhookCaCertReq: vi.fn(),
    deleteWebhookCaCertReq: vi.fn(),
  },
}))

vi.mock('@/pages/applicationManagement/mqtt-module', () => ({
  default: () => <div data-testid="mqtt-module-stub">MQTT module</div>,
}))

vi.mock('@/pages/applicationManagement/webhook-module', () => ({
  default: () => <div data-testid="webhook-module-stub">Webhook module</div>,
}))

vi.mock('@/pages/applicationManagement/linecrossing/VideoLineCanvas', () => ({
  default: () => <div data-testid="lc-video-stub">video</div>,
  draftToLineFields: () => null,
}))

const idleSession = () => envelopeOk({ state: 'idle', app_running: false, abi_version: 0, granted_caps: '0x00000000' })

const runningSession = () => envelopeOk({
    state: 'running',
    publisher_id: 'pub-alpha',
    app_id: 'hello-app',
    app_running: true,
    abi_version: 196608,
    granted_caps: '0x000000c8',
    event_max: 2048,
    last_result: 'OK',
  })

const noSnapshotUnavailable = () => envelopeOk({ availability: 'unavailable' })

const reportStatusUnbound = () => envelopeOk({ session_bound: false, session_state: 'running', host_boot_id: 0, queue_slots: 8, queue_used: 0, reports: [] })

const renderPage = () => render(
    <I18nWrapper>
      <ApplicationManagement />
    </I18nWrapper>,
  )

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

describe('v2/v3 trusted persistent session management in the manage dialog', () => {
  it('starts a session cooperatively and reflects the running state', async () => {
    let started = false
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/installed')) return Promise.resolve(installedState(false))
      if (u.includes('/api/v1/app/session/status')) return Promise.resolve(started ? runningSession() : idleSession())
      if (u.includes('/api/v1/app/business/snapshot')) return Promise.resolve(noSnapshotUnavailable())
      if (u.includes('/api/v1/app/report/status')) return Promise.resolve(reportStatusUnbound())
      if (u.includes('/api/v1/app/control/status')) return Promise.resolve(envelopeOk({ session_active: false }))
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/session/start')) {
        started = true
        return Promise.resolve(
          envelopeOk({ status: 'started', abi_version: 196608, granted_caps: '0x000000c8', event_max: 2048 }),
        )
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    const section = await screen.findByTestId('session-section')
    expect(section).toBeTruthy()
    expect(screen.getByTestId('session-state').textContent).toBe('Idle')
    const startBtn = screen.getByTestId('session-start') as HTMLButtonElement
    expect(startBtn.disabled).toBe(false)
    fireEvent.click(startBtn)
    await waitFor(() => {
      expect(useAppSessionStore.getState().outcome?.kind).toBe('started')
    })
    await waitFor(() => {
      expect(useAppSessionStore.getState().session?.state).toBe('running')
    })
  })

  it('surfaces a refused start with the host reason instead of fake success', async () => {
    stubGetInstalled(installedState(false))
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/installed')) return Promise.resolve(installedState(false))
      if (u.includes('/api/v1/app/session/status')) return Promise.resolve(idleSession())
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/session/start')) {
        return Promise.resolve(
          envelopeOk({ status: 'refused', reason: 'UNAVAILABLE', detail: 'ai busy', executable: false }),
        )
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('session-section')
    fireEvent.click(screen.getByTestId('session-start'))
    const outcome = await screen.findByTestId('session-outcome')
    expect(outcome.textContent).toContain('refused')
    expect(useAppSessionStore.getState().outcome?.reason).toBe('UNAVAILABLE')
  })

  it('blocks update and uninstall while the session is running or stopping', async () => {
    stubGetInstalled(installedState(true))
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/installed')) return Promise.resolve(installedState(true))
      if (u.includes('/api/v1/app/session/status')) return Promise.resolve(runningSession())
      if (u.includes('/api/v1/app/business/snapshot')) return Promise.resolve(noSnapshotUnavailable())
      if (u.includes('/api/v1/app/report/status')) return Promise.resolve(reportStatusUnbound())
      if (u.includes('/api/v1/app/control/status')) return Promise.resolve(envelopeOk({ session_active: true }))
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
    renderPage()
    await waitFor(() => {
      expect(useAppSessionStore.getState().session?.state).toBe('running')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('session-section')
    expect(screen.getByTestId('session-state').textContent).toBe('Running')
    const updateRow = screen.getByTestId('update-row') as HTMLButtonElement
    const uninstallRow = screen.getByTestId('uninstall-row') as HTMLButtonElement
    expect(updateRow.disabled).toBe(true)
    expect(uninstallRow.disabled).toBe(true)
    expect(screen.getByTestId('running-block-note')).toBeTruthy()
  })

  it('stops a running session and shows the busy stop-pending state truthfully', async () => {
    stubGetInstalled(installedState(true))
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/installed')) return Promise.resolve(installedState(true))
      if (u.includes('/api/v1/app/session/status')) return Promise.resolve(runningSession())
      if (u.includes('/api/v1/app/business/snapshot')) return Promise.resolve(noSnapshotUnavailable())
      if (u.includes('/api/v1/app/report/status')) return Promise.resolve(reportStatusUnbound())
      if (u.includes('/api/v1/app/control/status')) return Promise.resolve(envelopeOk({ session_active: true }))
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/session/stop')) {
        return Promise.resolve(
          envelopeOk({ status: 'stopping', entry_returned: false, busy: true, log: '', log_truncated: false }),
        )
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    renderPage()
    await waitFor(() => {
      expect(useAppSessionStore.getState().session?.state).toBe('running')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('session-section')
    const stopBtn = screen.getByTestId('session-stop') as HTMLButtonElement
    expect(stopBtn.disabled).toBe(false)
    fireEvent.click(stopBtn)
    const outcome = await screen.findByTestId('session-outcome')
    expect(outcome.textContent).toContain('accepted')
    expect(useAppSessionStore.getState().outcome?.kind).toBe('stopping')
  })

  it('keeps the session section admin-gated on 403 and never invents a business tab from a stale session', async () => {
    stubGetInstalled(installedState(false))
    requestMock.get.mockImplementation((url: string) => {
      const u = String(url)
      if (u.includes('/api/v1/app/installed')) return Promise.resolve(installedState(false))
      if (u.includes('/api/v1/app/session/status')) return Promise.resolve(envelopeFail('FORBIDDEN'))
      return Promise.reject(new Error(`unexpected GET ${u}`))
    })
    renderPage()
    await waitFor(() => {
      expect(useAppSessionStore.getState().sessionAuthBlocked).toBe('forbidden')
    })
    expect(screen.queryByTestId('slot-business')).toBeNull()
  })
})
