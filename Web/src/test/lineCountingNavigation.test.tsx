import { describe, it, expect, vi, beforeAll } from 'vitest';
import { render, screen } from '@testing-library/preact';
import { Navigate } from 'react-router-dom';
import '@testing-library/jest-dom';

import { navigationItems } from '../layout/pc/menu';
import { baseRoutes } from '../router';
import { setLocale, I18nWrapper } from '../i18n';
import { i18n as linguiCore } from '@lingui/core';
import ApplicationManagement from '../pages/applicationManagement/index';
import LineCountingModule from '../pages/applicationManagement/line-counting-module';

beforeAll(() => setLocale('zh'));

vi.mock('../services/request', () => ({
  default: {
    get: vi.fn().mockResolvedValue({ data: {} }),
    post: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

vi.mock('../lib/MSE/h264Player', () => ({
  default: class MockPlayer {
    enabled = true;

    initPlayer(): boolean {
      return this.enabled;
    }

    start(): boolean {
      return this.enabled;
    }

    destroy(): boolean {
      return this.enabled;
    }

    hardRestart(): boolean {
      return this.enabled;
    }

    resetStartState(): MockPlayer {
      return this;
    }
  },
}));

vi.mock('../services/api/deviceTool', () => ({
  default: {
    startVideoStreamReq: vi.fn().mockResolvedValue(undefined),
    stopVideoStreamReq: vi.fn().mockResolvedValue(undefined),
  },
}));

vi.mock('@lingui/react', () => ({
  useLingui: () => ({
    i18n: {
      _: (k: string) => (linguiCore.messages as Record<string, string>)[k] ?? k,
    },
  }),
  I18nProvider: ({ children }: { children: preact.ComponentChildren }) => children,
}));

vi.mock('../components/ui/button', () => ({
  Button: (props: Record<string, unknown>) => <button {...props} />,
}));

vi.mock('../components/ui/card', () => ({
  Card: (props: Record<string, unknown>) => <div {...props} />,
  CardContent: (props: Record<string, unknown>) => <div {...props} />,
}));

vi.mock('../components/ui/input', () => ({
  Input: (props: Record<string, unknown>) => <input {...props} />,
}));

vi.mock('../components/ui/label', () => ({
  Label: (props: Record<string, unknown>) => <label {...props} />,
}));

vi.mock('../components/ui/switch', () => ({
  Switch: ({ checked }: { checked?: boolean }) => (
    <input type="checkbox" role="switch" checked={checked} />
  ),
}));

vi.mock('../components/ui/tabs', () => ({
  Tabs: (props: Record<string, unknown>) => <div {...props} />,
  TabsList: (props: Record<string, unknown>) => <div {...props} />,
  TabsTrigger: (props: Record<string, unknown> & { value?: string }) => <div role="tab" {...props} />,
  TabsContent: (props: Record<string, unknown> & { value?: string }) => <div role="tabpanel" {...props} />,
}));

class ResizeObserverStub {
  cb: ResizeObserverCallback;

  constructor(cb: ResizeObserverCallback) {
    this.cb = cb;
  }

  observe(): void {
    this.cb([{ contentRect: { width: 800, height: 450 } }] as ResizeObserverEntry[], this as unknown as ResizeObserver);
  }

  unobserve(): boolean {
    return this.cb !== null;
  }

  disconnect(): boolean {
    return this.cb !== null;
  }
}
(globalThis as Record<string, unknown>).ResizeObserver = ResizeObserverStub;

vi.mock('../pages/applicationManagement/line-counting-module', () => ({
  default: () => <div>line-counting-module-stub</div>,
}));

vi.mock('../pages/applicationManagement/mqtt-module', () => ({
  default: () => <div>mqtt-module-stub</div>,
}));

vi.mock('../pages/applicationManagement/webhook-module', () => ({
  default: () => <div>webhook-module-stub</div>,
}));

describe('legacy navigation cleanup', () => {
  it('top-level menu has no people counting entry', () => {
    expect(navigationItems.some((item) => item.path === '/people-counting')).toBe(false);
    expect(navigationItems.some((item) => item.key.includes('people_counting'))).toBe(false);
    expect(navigationItems.some((item) => item.path === '/application-management')).toBe(true);
  });

  it('legacy /people-counting route is a redirect to application management', () => {
    const legacy = baseRoutes.find((route) => route.path === '/people-counting');
    expect(legacy).toBeDefined();
    expect((legacy?.element as { type?: unknown })?.type).toBe(Navigate);
  });

  it('application management hosts the three sibling functions', () => {
    render(<I18nWrapper><ApplicationManagement /></I18nWrapper>);
    expect(screen.getByText('过线统计')).toBeInTheDocument();
    expect(screen.getByText('MQTT/MQTTS')).toBeInTheDocument();
    expect(screen.getByText('Webhook')).toBeInTheDocument();
    expect(screen.getByText('mqtt-module-stub')).toBeInTheDocument();
    expect(screen.getByText('line-counting-module-stub')).toBeInTheDocument();
    expect(LineCountingModule).toBeDefined();
  });
});
