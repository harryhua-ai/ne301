import { describe, it, expect, vi, beforeEach } from 'vitest';
import { render, screen, fireEvent, waitFor } from '@testing-library/preact';
import '@testing-library/jest-dom';

import ApplicationManagement from '../pages/applicationManagement/index';
import LineCountingModule from '../pages/applicationManagement/line-counting-module';
import VideoPreview from '../pages/applicationManagement/lineCounting/VideoPreview';
import type { LineCountingConfig, LineCountingStatus } from '../services/api/line-counting';

const getConfig = vi.fn();
const getStatus = vi.fn();
const getStats = vi.fn();
const getEvents = vi.fn();
const setConfig = vi.fn();
const reset = vi.fn();

vi.mock('../services/api/line-counting', () => ({
  default: {
    getConfig: (...a: unknown[]) => getConfig(...a),
    setConfig: (...a: unknown[]) => setConfig(...a),
    getStatus: (...a: unknown[]) => getStatus(...a),
    getStats: (...a: unknown[]) => getStats(...a),
    getEvents: (...a: unknown[]) => getEvents(...a),
    reset: (...a: unknown[]) => reset(...a),
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
  Switch: ({ checked, ...rest }: Record<string, unknown> & { checked?: boolean }) => (
    <input type="checkbox" role="switch" checked={checked} {...rest} />
  ),
}));

vi.mock('../components/ui/tabs', () => ({
  Tabs: (props: Record<string, unknown>) => <div {...props} />,
  TabsList: (props: Record<string, unknown>) => <div {...props} />,
  TabsTrigger: (props: Record<string, unknown> & { value?: string }) => <div role="tab" {...props} />,
  TabsContent: (props: Record<string, unknown> & { value?: string }) => <div role="tabpanel" {...props} />,
}));

vi.mock('lucide-react', () => ({
  RefreshCw: () => <span>refresh</span>,
}));

vi.mock('../pages/applicationManagement/mqtt-module', () => ({
  default: () => <div>mqtt-module-stub</div>,
}));

vi.mock('../pages/applicationManagement/webhook-module', () => ({
  default: () => <div>webhook-module-stub</div>,
}));

vi.mock('@lingui/react', () => ({
  useLingui: () => ({ i18n: { _: (k: string) => k } }),
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

const cfg: LineCountingConfig = {
  enable: true,
  counter_name: '客流统计',
  target_class: 'person',
  line: { x1: 0.2, y1: 0.5, x2: 0.8, y2: 0.5, outside_x: 0.5, outside_y: 0.2 },
  confidence_threshold: 0.25,
  tracking: { association_distance: 0.25, history_length: 8, max_missed_frames: 5, confirmation_frames: 5 },
  window_minutes: 5,
  reporting: {
    mqtt_enabled: true,
    webhook_enabled: false,
    tracks_enabled: true,
    heat_grid_enabled: false,
    backlog_capacity: 24,
  },
};

const status: LineCountingStatus = {
  enabled: true,
  state: 'running',
  reason: null,
  target_class: 'person',
  model: {
    name: 'person_yolo_v2',
    version: '2.0.0',
    postprocess_type: 'pp_od_yolo_v8_uf',
    generation: 3,
    classes: [
      { id: 0, name: 'person' },
      { id: 1, name: 'car' },
    ],
  },
};

function mockApi() {
  getConfig.mockResolvedValue({ data: cfg });
  getStatus.mockResolvedValue({ data: status });
  getStats.mockResolvedValue({ data: { window: { in: 3, out: 1 }, total: { in: 30, out: 10 }, delivery: { mqtt: { backlog: 0, dropped: 0 }, webhook: { backlog: 0, dropped: 0 } } } });
  getEvents.mockResolvedValue({ data: { events: [] } });
}

describe('application management tabs', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mockApi();
  });

  it('shows 过线统计 / MQTT/MQTTS / Webhook as sibling tabs and renders the module', async () => {
    render(<ApplicationManagement />);
    expect(screen.getByText('过线统计')).toBeInTheDocument();
    expect(screen.getByText('MQTT/MQTTS')).toBeInTheDocument();
    expect(screen.getByText('Webhook')).toBeInTheDocument();
    await waitFor(() => expect(getConfig).toHaveBeenCalled());
  });
});

describe('line counting module draft/save behavior', () => {
  beforeEach(() => {
    vi.clearAllMocks();
    mockApi();
  });

  it('does not POST while editing draft; one complete POST on Save', async () => {
    render(<LineCountingModule />);
    await waitFor(() => expect(getConfig).toHaveBeenCalled());

    fireEvent.change(await screen.findByDisplayValue('客流统计'), {
      target: { value: '北门客流' },
    });
    expect(setConfig).not.toHaveBeenCalled();

    fireEvent.click(screen.getByRole('button', { name: '保存' }));
    await waitFor(() => expect(setConfig).toHaveBeenCalledTimes(1));
    const saved = setConfig.mock.calls[0][0] as LineCountingConfig;
    expect(saved.counter_name).toBe('北门客流');
    expect(saved.line.x1).toBe(0.2);
  });

  it('target-class change requires confirmation dialog before POST', async () => {
    render(<LineCountingModule />);
    await waitFor(() => expect(getConfig).toHaveBeenCalled());

    fireEvent.change(await screen.findByDisplayValue('person'), {
      target: { value: 'car' },
    });
    fireEvent.click(screen.getByRole('button', { name: '保存' }));
    await waitFor(() => expect(screen.getByTestId('lc-confirm-dialog')).toBeInTheDocument());
    expect(setConfig).not.toHaveBeenCalled();

    fireEvent.click(screen.getByRole('button', { name: '确认' }));
    await waitFor(() => expect(setConfig).toHaveBeenCalledTimes(1));
  });
});

describe('video overlay', () => {
  it('renders line/arrow/track primitives and runtime badge, no bbox or track id', () => {
    const widthSpy = vi.spyOn(HTMLElement.prototype, 'clientWidth', 'get').mockReturnValue(800);
    const heightSpy = vi.spyOn(HTMLElement.prototype, 'clientHeight', 'get').mockReturnValue(450);
    const { container } = render(
      <VideoPreview
        config={cfg}
        draft={cfg}
        editMode={false}
        editPhase={0}
        state="running"
        tracks={[{ track_id: 57, points: [{ x: 0.5, y: 0.4 }, { x: 0.5, y: 0.6 }] }]}
        onPickPoint={() => {}}
      />,
    );
    container.querySelector('video')?.dispatchEvent(
      new Event('loadedmetadata', { bubbles: true }),
    );
    expect(screen.getByTestId('lc-runtime-badge')).toHaveTextContent('运行中');
    expect(screen.getByTestId('lc-track')).toBeInTheDocument();
    expect(screen.getByTestId('lc-overlay')).toBeInTheDocument();
    expect(container.querySelector('polyline')).toBeInTheDocument();
    expect(screen.queryByText(/track/i)).not.toBeInTheDocument();
    expect(screen.queryByText(/IN/)).not.toBeInTheDocument();
    expect(screen.queryByText(/OUT/)).not.toBeInTheDocument();
    widthSpy.mockRestore();
    heightSpy.mockRestore();
  });
});
