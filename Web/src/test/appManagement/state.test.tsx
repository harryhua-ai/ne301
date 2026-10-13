import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import AppManagement from '@/pages/appManagement'
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
  selectNeappFile,
  makeNeappFile,
} from './helpers'

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
}))

vi.mock('sonner', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() },
}))

const renderPage = () => render(<I18nWrapper><AppManagement /></I18nWrapper>)

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('GET /api/v1/app/installed three-state presentation', () => {
  it('shows installed identity and enables run and uninstall', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(await screen.findByText('pub-alpha')).toBeTruthy()
    expect(screen.getByText('hello-app')).toBeTruthy()
    expect(screen.getByText('v1.2.3')).toBeTruthy()
    expect(screen.getByText('bb22')).toBeTruthy()
    const runBtn = screen.getByTestId('run-button') as HTMLButtonElement
    const uninstallBtn = screen.getByTestId('uninstall-button') as HTMLButtonElement
    expect(runBtn.disabled).toBe(false)
    expect(uninstallBtn.disabled).toBe(false)
    expect(screen.getByTestId('runtime-state').textContent).toBe('Idle')
  })

  it('shows unavailable with retained identity and device reason, run disabled, no auto delete', async () => {
    stubGetInstalled(unavailableState('ABI_INCOMPATIBLE'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    expect(await screen.findByText('pub-alpha')).toBeTruthy()
    expect(screen.getByText('ABI_INCOMPATIBLE')).toBeTruthy()
    const runBtn = screen.getByTestId('run-button') as HTMLButtonElement
    expect(runBtn.disabled).toBe(true)
    expect(requestMock.delete).not.toHaveBeenCalled()
  })

  it('shows not installed only when the device proves absence', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(await screen.findByText('Not Installed')).toBeTruthy()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByTestId('uninstall-button') as HTMLButtonElement).disabled).toBe(true)
  })

  it('maps state unknown to undetermined, never to not installed, and blocks all dangerous actions', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(await screen.findByTestId('undetermined-warning')).toBeTruthy()
    expect(screen.queryByText('Not Installed')).toBeNull()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByTestId('uninstall-button') as HTMLButtonElement).disabled).toBe(true)
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    expect(input.disabled).toBe(false)
    selectNeappFile(input, makeNeappFile([1, 2, 3], 'pkg.neapp'))
    await waitFor(() => {
      const installBtn = screen.getByText('Install', { selector: 'button' }) as HTMLButtonElement
      expect(installBtn.disabled).toBe(true)
    })
    expect(screen.queryByText(/reinstall/i)).toBeNull()
  })

  it('normalizes backend responses that carry status instead of state', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
  })

  it('shows undetermined when the state query itself fails at network level', async () => {
    requestMock.get.mockImplementation(() => Promise.reject(new Error('Network Error')))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(await screen.findByTestId('undetermined-warning')).toBeTruthy()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(true)
  })

  it('hides sensitive identity when the query is refused as forbidden', async () => {
    requestMock.get.mockImplementation(() =>
      Promise.resolve(envelopeFail('FORBIDDEN', 'High-privilege administrator authentication required')),
    )
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('forbidden')
    })
    expect(useAppManagementStore.getState().view).toBeNull()
    expect(screen.queryByText('pub-alpha')).toBeNull()
    expect(await screen.findByText(/do not have high-privilege app management permission/i)).toBeTruthy()
  })

  it('hides sensitive identity and prompts relogin when unauthorized', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve(envelopeFail('UNAUTHORIZED')))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('unauthorized')
    })
    expect(useAppManagementStore.getState().view).toBeNull()
    expect(screen.queryByText('pub-alpha')).toBeNull()
    expect(await screen.findByText('Log in again')).toBeTruthy()
  })
})
