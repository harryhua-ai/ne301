import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import AppManagement from '@/pages/appManagement'
import { useAppManagementStore } from '@/store/appManagement'
import {
  requestMock,
  installedState,
  noneState,
  stubGetInstalled,
  stubPath,
  setupSuite,
  teardownSuite,
  envelopeOk,
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

const selectFile = async (bytes = [1, 2, 3, 4]) => {
  const input = screen.getByTestId('neapp-input') as HTMLInputElement
  const file = makeNeappFile(bytes, 'hello-v2.neapp')
  selectNeappFile(input, file)
  await waitFor(() => {
    expect((screen.getByText('Install', { selector: 'button' }) as HTMLButtonElement).disabled).toBe(false)
  })
  return file
}

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('install flow', () => {
  it('sends package_base64 only and adopts server-confirmed state after refresh', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/install', () =>
      Promise.resolve(
        envelopeOk({
          status: 'installed',
          publisher_id: 'pub-alpha',
          app_id: 'hello-app',
          version_major: 1,
          version_minor: 2,
          version_patch: 3,
          package_len: 4,
          package_sha256: 'aa',
          tbs_sha256: 'bb',
          executable: false,
        }),
      ),
    )
    const file = await selectFile([77, 88])
    fireEvent.click(screen.getByText('Install', { selector: 'button' }))
    await waitFor(() => {
      expect(useAppManagementStore.getState().installOutcome).not.toBeNull()
    })
    expect(requestMock.post).toHaveBeenCalledTimes(1)
    const [url, body] = requestMock.post.mock.calls[0]
    expect(String(url)).toBe('/api/v1/app/install')
    expect(Object.keys(body as object)).toEqual(['package_base64'])
    const expected = btoa(String.fromCharCode(...new Uint8Array([77, 88])))
    expect((body as { package_base64: string }).package_base64).toBe(expected)
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(file).toBeTruthy()
  })

  it('treats upload completion without server confirmation as unknown and verifies with GET', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/install')) {
        return Promise.reject(new Error('timeout of 20000ms exceeded'))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    await selectFile()
    fireEvent.click(screen.getByText('Install', { selector: 'button' }))
    await waitFor(() => {
      expect(useAppManagementStore.getState().lastError?.kind).toBe('indeterminate')
    })
    expect(useAppManagementStore.getState().pendingVerification).toBe(false)
    expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    expect(useAppManagementStore.getState().installOutcome).toBeNull()
    expect(screen.queryByTestId('install-outcome')).toBeNull()
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('keeps the pending verification notice while the verifying query cannot complete', async () => {
    let releaseQuery!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseQuery = resolve
    })
    let call = 0
    requestMock.get.mockImplementation((url: string) => {
      call += 1
      if (String(url).includes('/api/v1/app/installed')) {
        if (call === 1) {
          return Promise.resolve(noneState())
        }
        return new Promise((resolve) => {
          gate.then(() => resolve(noneState()))
        })
      }
      return Promise.reject(new Error(`unexpected GET ${url}`))
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/install')) {
        return Promise.reject(new Error('timeout of 20000ms exceeded'))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    await selectFile()
    fireEvent.click(screen.getByText('Install', { selector: 'button' }))
    await waitFor(() => {
      expect(useAppManagementStore.getState().pendingVerification).toBe(true)
    })
    expect(useAppManagementStore.getState().installOutcome).toBeNull()
    expect(await screen.findByTestId('pending-verification')).toBeTruthy()
    releaseQuery()
  })

  it('rejects oversized files locally without any device write', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    const big = makeNeappFile([], 'big.neapp')
    Object.defineProperty(big, 'size', { value: 2097401 })
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, big)
    await waitFor(() => {
      expect((screen.getByText('Install', { selector: 'button' }) as HTMLButtonElement).disabled).toBe(false)
    })
    fireEvent.click(screen.getByText('Install', { selector: 'button' }))
    await waitFor(() => {
      expect(useAppManagementStore.getState().lastError?.kind).toBe('file')
    })
    expect(requestMock.post).not.toHaveBeenCalled()
  })
})

describe('uninstall flow', () => {
  it('requires explicit confirmation before DELETE is sent', async () => {
    stubGetInstalled(installedState(false))
    stubPath('delete', '/api/v1/app/installed', () =>
      Promise.resolve(
        envelopeOk({
          status: 'uninstalled',
          publisher_id: 'pub-alpha',
          app_id: 'hello-app',
          version_major: 1,
          version_minor: 2,
          version_patch: 3,
          package_len: 1024,
          package_sha256: 'aa11',
          tbs_sha256: 'bb22',
          executable: false,
        }),
      ),
    )
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('uninstall-button'))
    expect(await screen.findByText('Uninstall the committed app?')).toBeTruthy()
    expect(requestMock.delete).not.toHaveBeenCalled()
    fireEvent.click(screen.getByText('Cancel'))
    await waitFor(() => {
      expect(screen.queryByText('Uninstall the committed app?')).toBeNull()
    })
    expect(requestMock.delete).not.toHaveBeenCalled()
    fireEvent.click(screen.getByTestId('uninstall-button'))
    expect(await screen.findByText('Uninstall the committed app?')).toBeTruthy()
    fireEvent.click(screen.getByTestId('confirm-uninstall'))
    await waitFor(() => {
      expect(requestMock.delete).toHaveBeenCalledTimes(1)
    })
    expect(String(requestMock.delete.mock.calls[0][0])).toBe('/api/v1/app/installed')
  })

  it('keeps the committed app visible when uninstall is refused BUSY', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    stubPath('delete', '/api/v1/app/installed', () =>
      Promise.resolve({ success: true, data: { status: 'refused', reason: 'BUSY', executable: false } }),
    )
    await useAppManagementStore.getState().uninstallApp()
    const s = useAppManagementStore.getState()
    expect(s.uninstallOutcome).toBeNull()
    expect(s.lastError).toEqual({ scope: 'uninstall', kind: 'refused', reason: 'BUSY' })
    expect(s.view?.kind).toBe('installed')
  })
})

describe('manual single run flow', () => {
  it('shows the app entry result and session-bounded nature after a completed run', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    stubPath('post', '/api/v1/app/run', () =>
      Promise.resolve(
        envelopeOk({
          status: 'completed',
          publisher_id: 'pub-alpha',
          app_id: 'hello-app',
          version_major: 1,
          version_minor: 2,
          version_patch: 3,
          package_len: 1024,
          package_sha256: 'aa11',
          tbs_sha256: 'bb22',
          executable: false,
          entry_returned: true,
          entry_result: 0,
          log: 'hello from app',
          log_truncated: false,
          app_running: false,
        }),
      ),
    )
    fireEvent.click(screen.getByTestId('run-button'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().runOutcome?.status).toBe('completed')
    })
    expect((screen.getByTestId('run-outcome') as HTMLElement).textContent).toContain('0')
    expect((screen.getByTestId('run-outcome') as HTMLElement).textContent).toContain('hello from app')
    expect((screen.getByTestId('run-outcome') as HTMLElement).textContent).toContain('browser session')
  })

  it('disables run while the device reports app_running and never offers a force stop', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('runtime-state').textContent).toBe('Running')
    const buttons = screen.getAllByRole('button') as HTMLButtonElement[]
    expect(buttons.some((b) => /stop/i.test(b.textContent || ''))).toBe(false)
  })

  it('ignores overlapping run requests while one is in flight', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    let releaseRun!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseRun = resolve
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/run')) {
        return gate.then(() =>
          Promise.resolve(
            envelopeOk({ status: 'completed', entry_returned: true, entry_result: 0, app_running: false }),
          ),
        )
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    const first = useAppManagementStore.getState().runOnce()
    const second = useAppManagementStore.getState().runOnce()
    await Promise.resolve()
    expect(useAppManagementStore.getState().running).toBe(true)
    releaseRun()
    await Promise.all([first, second])
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('shows host failure class separately from any app return value when run is refused', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    stubPath('post', '/api/v1/app/run', () =>
      Promise.resolve({
        success: true,
        data: { status: 'refused', reason: 'LOAD_FAILED', detail: 'segment fault in load', executable: false },
      }),
    )
    fireEvent.click(screen.getByTestId('run-button'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().runOutcome?.status).toBe('refused')
    })
    const panel = screen.getByTestId('run-outcome').textContent || ''
    expect(panel).toContain('failed to load the app')
    expect(panel).toContain('segment fault in load')
  })
})
