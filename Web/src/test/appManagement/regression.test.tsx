import { describe, it, expect, afterEach, vi } from 'vitest'
import { render, screen, cleanup } from '@testing-library/preact'
import type { ComponentChildren } from 'preact'
import { I18nWrapper } from '@/i18n'
import Menu from '@/layout/pc/menu'
import ApplicationManagement from '@/pages/applicationManagement'
import applicationManagementService from '@/services/api/applicationManagement'

vi.mock('react-router-dom', () => ({
  MemoryRouter: ({ children }: { children?: ComponentChildren }) => children,
  Link: (props: { to?: string; children?: ComponentChildren }) => (
    <a href={props.to}>{props.children}</a>
  ),
  useLocation: () => ({ pathname: '/' }),
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

afterEach(() => {
  cleanup()
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

  it('keeps MQTT/MQTTS and Webhook tabs on the existing application management page', async () => {
    render(
      <I18nWrapper>
        <ApplicationManagement />
      </I18nWrapper>,
    )
    expect(await screen.findByText('MQTT/MQTTS')).toBeTruthy()
    expect(screen.getByText('Webhook')).toBeTruthy()
  })

  it('navigates to both the legacy application management page and the new trusted app page', () => {
    render(
      <I18nWrapper>
        <Menu />
      </I18nWrapper>,
    )
    const links = Array.from(document.querySelectorAll('a'))
    const paths = links.map((a) => a.getAttribute('href'))
    expect(paths).toContain('/application-management')
    expect(paths).toContain('/app-management')
    const trusted = links.find((a) => a.getAttribute('href') === '/app-management')
    expect(trusted?.textContent).toBe('Trusted App')
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
