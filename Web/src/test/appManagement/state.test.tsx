import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import {
  requestMock,
  installedState,
  noneState,
  unknownState,
  unavailableState,
  stubGetInstalled,
  setupSuite,
  teardownSuite,
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

const renderPage = () => render(
    <I18nWrapper>
      <ApplicationManagement />
    </I18nWrapper>,
  )

const openSlotDialog = async (slotTestIds: string, contentTestId: string) => {
  fireEvent.click(screen.getByTestId(slotTestIds))
  await screen.findByTestId(contentTestId)
}

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('device state drives the first slot and safe dialogs', () => {
  it('shows the manage slot for an installed app without inventing a business tab', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.queryByText('Line Counting')).toBeNull()
    expect(screen.getByTestId('no-business-notice')).toBeTruthy()
    await openSlotDialog('slot-manage', 'manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('hello-app')
    expect(screen.getByText('pub-alpha')).toBeTruthy()
    expect(screen.getByText('v1.2.3')).toBeTruthy()
    expect(screen.getByText('bb22')).toBeTruthy()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.getByTestId('runtime-state').textContent).toBe('Idle')
  })

  it('shows the status slot for an unavailable app with retained identity and device reason', async () => {
    stubGetInstalled(unavailableState('ABI_INCOMPATIBLE'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    expect(screen.getByTestId('slot-status')).toBeTruthy()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
    expect(screen.getByTestId('unavailable-warning').textContent).toContain('ABI_INCOMPATIBLE')
    await openSlotDialog('slot-status', 'status-reason')
    expect(screen.getByTestId('status-reason').textContent).toBe('ABI_INCOMPATIBLE')
    expect(screen.getByText('pub-alpha')).toBeTruthy()
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
    expect(requestMock.delete).not.toHaveBeenCalled()
  })

  it('shows the deploy slot only when the device proves absence', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.getByTestId('slot-deploy')).toBeTruthy()
    expect(screen.queryByTestId('slot-status')).toBeNull()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
    await openSlotDialog('slot-deploy', 'deploy-confirm')
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect(screen.queryByTestId('uninstall-row')).toBeNull()
    expect((screen.getByTestId('deploy-confirm') as HTMLButtonElement).disabled).toBe(true)
  })

  it('maps state unknown to the status slot, never to deploy, and blocks all dangerous actions', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(screen.getByTestId('slot-status')).toBeTruthy()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.getByTestId('undetermined-warning')).toBeTruthy()
    await openSlotDialog('slot-status', 'status-refresh')
    expect(screen.getByText(/storage state cannot be determined/i)).toBeTruthy()
    expect(screen.queryByTestId('uninstall-row')).toBeNull()
    expect(screen.queryByTestId('update-row')).toBeNull()
    expect(screen.queryByTestId('neapp-input')).toBeNull()
    expect((screen.getByTestId('status-refresh') as HTMLButtonElement).disabled).toBe(false)
  })

  it('normalizes backend responses that carry status instead of state', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
  })

  it('shows the status slot when the state query itself fails at network level', async () => {
    requestMock.get.mockImplementation(() => Promise.reject(new Error('Network Error')))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(screen.getByTestId('slot-status')).toBeTruthy()
    expect(screen.getByTestId('undetermined-warning')).toBeTruthy()
  })

  it('hides sensitive identity and shows the forbidden notice without a device verdict', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve(envelopeFail('FORBIDDEN', 'High-privilege administrator authentication required')),)
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('forbidden')
    })
    expect(useAppManagementStore.getState().view).toBeNull()
    expect(screen.queryByText('pub-alpha')).toBeNull()
    expect(screen.getByTestId('slot-status')).toBeTruthy()
    expect(screen.getByText(/do not have high-privilege app management permission/i)).toBeTruthy()
  })

  it('hides sensitive identity and prompts relogin when unauthorized', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve(envelopeFail('UNAUTHORIZED')))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('unauthorized')
    })
    expect(useAppManagementStore.getState().view).toBeNull()
    expect(screen.queryByText('pub-alpha')).toBeNull()
    const relogin = screen.getByTestId('relogin-link')
    expect(relogin.getAttribute('href')).toBe('/login')
  })
})
