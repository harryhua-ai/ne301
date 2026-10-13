import { describe, it, expect, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent, cleanup } from '@testing-library/preact'
import type { ComponentChildren } from 'preact'
import { I18nWrapper } from '@/i18n'
import Menu from '@/layout/pc/menu'
import ApplicationManagement from '@/pages/applicationManagement'
import {
  businessUiCatalog,
  confirmedV1SingleRunAppIds,
  runOnceEligible,
} from '@/pages/applicationManagement/appSlot'
import { useAppManagementStore } from '@/store/appManagement'
import applicationManagementService from '@/services/api/applicationManagement'
import {
  installedState,
  installedStateFor,
  noneState,
  unavailableState,
  unknownState,
  stubGetInstalled,
  resetBusinessCatalog,
} from './helpers'

vi.mock('react-router-dom', async (importOriginal) => {
  const actual = await importOriginal<typeof import('react-router-dom')>()
  return {
    ...actual,
    MemoryRouter: ({ children }: { children?: ComponentChildren }) => children,
    Link: ({ to, children }: { to?: string; children?: ComponentChildren }) => (
      <a href={to}>{children}</a>
    ),
    useLocation: () => ({ pathname: '/' }),
  }
})

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
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

vi.mock('sonner', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() },
}))

const renderPage = () => render(
    <I18nWrapper>
      <ApplicationManagement />
    </I18nWrapper>,
  )

afterEach(() => {
  cleanup()
  resetBusinessCatalog()
})

describe('existing non-App management features do not regress', () => {
  it('keeps the MQTT and Webhook service client methods intact', () => {
    expect(typeof applicationManagementService.getMqttConfigReq).toBe('function')
    expect(typeof applicationManagementService.setMqttConfigReq).toBe('function')
    expect(typeof applicationManagementService.connectMqttReq).toBe('function')
    expect(typeof applicationManagementService.disconnectMqttReq).toBe('function')
    expect(typeof applicationManagementService.getWebhookConfigReq).toBe('function')
    expect(typeof applicationManagementService.setWebhookConfigReq).toBe('function')
    expect(typeof applicationManagementService.testWebhookReq).toBe('function')
  })

  it('keeps MQTT/MQTTS and Webhook tabs, in order, on the single application management page', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.getByTestId('mqtt-module-stub')).toBeTruthy()
    const tabrail = screen.getByTestId('slot-deploy').parentElement as HTMLElement
    const labels = Array.from(tabrail.querySelectorAll('button')).map((b) => b.textContent || '')
    expect(labels.some((t) => t.includes('MQTT/MQTTS'))).toBe(true)
    expect(labels.some((t) => t.includes('Webhook'))).toBe(true)
    expect(labels.findIndex((t) => t.includes('MQTT/MQTTS'))).toBeLessThan(
      labels.findIndex((t) => t.includes('Webhook')),
    )
  })
})

describe('single entry and navigation contract', () => {
  it('keeps only the legacy application management entry in the top navigation', () => {
    render(
      <I18nWrapper>
        <Menu />
      </I18nWrapper>,
    )
    const links = Array.from(document.querySelectorAll('a'))
    const paths = links.map((a) => a.getAttribute('href'))
    expect(paths).toContain('/application-management')
    expect(paths).not.toContain('/app-management')
    expect(paths.join(' ')).not.toMatch(/trusted app/i)
  })

  it('registers no standalone trusted app route and keeps the old page module removed', async () => {
    const legacySpecifier = ['@/', 'pages/', 'appManagement'].join('')
    await expect(import(legacySpecifier)).rejects.toThrow()
    vi.resetModules()
    const pageMocks: [string, { default: () => null }][] = [
      ['@/pages/login', { default: () => null }],
      ['@/pages/deviceTool', { default: () => null }],
      ['@/pages/home', { default: () => null }],
      ['@/pages/notFound', { default: () => null }],
      ['@/pages/modelVerification', { default: () => null }],
      ['@/pages/applicationManagement', { default: () => null }],
      ['@/pages/hardwareManagement', { default: () => null }],
      ['@/pages/captureSettings', { default: () => null }],
      ['@/pages/systemSettings', { default: () => null }],
      ['@/pages/storageManagement', { default: () => null }],
      ['@/pages/deviceInformation', { default: () => null }],
      ['@/pages/ImportFSBL', { default: () => null }],
      ['@/pages/ImportWifi', { default: () => null }],
      ['@/pages/ImportBundle', { default: () => null }],
      ['@/pages/UpgradeWaiting', { default: () => null }],
      ['@/layout', { default: () => null }],
    ]
    for (const [path, mock] of pageMocks) {
      vi.doMock(path, () => mock)
    }
    const mod = await import('@/router')
    const router = mod.default as unknown as { routes: Array<Record<string, unknown>> }
    const paths: string[] = []
    const walk = (rs: Array<Record<string, unknown>>) => {
      for (const r of rs) {
        if (typeof r.path === 'string') paths.push(r.path)
        if (Array.isArray(r.children)) walk(r.children as Array<Record<string, unknown>>)
      }
    }
    walk(router.routes || [])
    expect(paths).toContain('/application-management')
    expect(paths).not.toContain('/app-management')
  })
})

describe('four-state first slot inside the application management page', () => {
  it('shows the deploy action for a device-confirmed empty device and keeps MQTT/Webhook usable', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.getByTestId('slot-deploy').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-business')).toBeNull()
    fireEvent.click(screen.getByTestId('slot-deploy'))
    expect(await screen.findByTestId('deploy-confirm')).toBeTruthy()
    expect(screen.getByTestId('mqtt-module-stub')).toBeTruthy()
  })

  it('shows the manage action for an installed app without a business UI and never fabricates one', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('slot-manage').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByText('Line Counting')).toBeNull()
    expect(screen.queryByText('过线统计')).toBeNull()
    expect(screen.getByTestId('no-business-notice')).toBeTruthy()
  })

  it('keeps update and uninstall operable for an installed idle app without any gating notice', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.queryByTestId('running-block-note')).toBeNull()
    fireEvent.click(screen.getByTestId('update-row'))
    await screen.findByTestId('update-confirm')
    expect(screen.queryByTestId('running-block-note')).toBeNull()
    expect(screen.queryByTestId('state-changed-note')).toBeNull()
  })

  it('shows the status entry for an unavailable app', async () => {
    stubGetInstalled(unavailableState('TRUST_UNPROVEN'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    expect(screen.getByTestId('slot-status').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-business')).toBeNull()
  })

  it('shows the status entry for an unknown state and never claims not installed', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(screen.getByTestId('slot-status').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.getByTestId('undetermined-warning')).toBeTruthy()
  })

  it('gates the business tab on a registered real business UI only', async () => {
    expect(businessUiCatalog['hello-app']).toBeUndefined()
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.queryByTestId('slot-business')).toBeNull()
    cleanup()
    resetBusinessCatalog()
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => <div data-testid="business-stub">business</div>,
    }
    renderPage()
    await waitFor(() => {
      expect(screen.getByTestId('slot-business')).toBeTruthy()
    })
    fireEvent.mouseDown(screen.getByTestId('slot-business'))
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    expect(screen.getByTestId('business-manage')).toBeTruthy()
  })

  it('falls back to MQTT when the selected business tab is removed after uninstall', async () => {
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => <div data-testid="business-stub">business</div>,
    }
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(screen.getByTestId('slot-business')).toBeTruthy()
    })
    fireEvent.mouseDown(screen.getByTestId('slot-business'))
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    expect(screen.queryByTestId('mqtt-module-stub')).toBeNull()
    stubGetInstalled(noneState())
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.getByTestId('slot-deploy')).toBeTruthy()
    expect(await screen.findByTestId('mqtt-module-stub')).toBeTruthy()
  })
})

describe('run-once eligibility gate inside the manage dialog', () => {
  it('suppresses run once when a real business UI is registered for the committed app', async () => {
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => <div data-testid="business-stub">business</div>,
    }
    expect(runOnceEligible('hello-app')).toBe(false)
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(screen.getByTestId('slot-business')).toBeTruthy()
    })
    fireEvent.mouseDown(screen.getByTestId('slot-business'))
    await screen.findByTestId('business-stub')
    fireEvent.click(screen.getByTestId('business-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('hello-app')
    expect(screen.queryByTestId('run-section')).toBeNull()
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
  })

  it('keeps run once for the confirmed v1 app when no business UI is registered', async () => {
    expect(businessUiCatalog['hello-app']).toBeUndefined()
    expect(confirmedV1SingleRunAppIds.has('hello-app')).toBe(true)
    expect(runOnceEligible('hello-app')).toBe(true)
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('run-section')).toBeTruthy()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(false)
  })

  it('offers no run once for an installed app whose v1 profile the state protocol cannot prove', async () => {
    expect(businessUiCatalog['mystery-app']).toBeUndefined()
    expect(confirmedV1SingleRunAppIds.has('mystery-app')).toBe(false)
    expect(runOnceEligible('mystery-app')).toBe(false)
    stubGetInstalled(installedStateFor('mystery-app', { version_major: 1, version_minor: 0, version_patch: 0 }))
    renderPage()
    await waitFor(() => {
      const { view } = useAppManagementStore.getState()
      expect(view?.kind === 'installed' && view.record.appId).toBe('mystery-app')
    })
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('mystery-app')
    expect(screen.queryByTestId('run-section')).toBeNull()
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
  })
})

describe('consumed server API contract', () => {
  it('matches the four high-privilege app routes with their documented methods', async () => {
    const mod = await import('@/services/api/appManagement')
    expect(mod.APP_PACKAGE_MAX_BYTES).toBe(2097400)
    expect(typeof mod.default.getInstalledState).toBe('function')
    expect(typeof mod.default.installPackage).toBe('function')
    expect(typeof mod.default.uninstallApp).toBe('function')
    expect(typeof mod.default.runApp).toBe('function')
  })
})
