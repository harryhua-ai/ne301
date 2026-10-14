import { afterEach, describe, expect, it, vi } from 'vitest';
import { cleanup, fireEvent, render } from '@testing-library/preact';
import { I18nWrapper } from '@/i18n';
import VideoPreview from '@/pages/applicationManagement/linecrossing/VideoPreview';

vi.mock('@/lib/MSE/h264Player', () => ({
  default: class {
    initPlayer = vi.fn();

    start = vi.fn().mockReturnThis();

    hardRestart = vi.fn();

    resetStartState = vi.fn().mockReturnThis();

    destroy = vi.fn();
  },
}));

vi.mock('@/services/api/deviceTool', () => ({
  default: {
    startVideoStreamReq: vi.fn().mockResolvedValue(undefined),
    stopVideoStreamReq: vi.fn().mockResolvedValue(undefined),
  },
}));

vi.mock('lucide-react', () => ({ RefreshCw: () => null }));

const originalResizeObserver = globalThis.ResizeObserver;
const originalClientWidth = Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'clientWidth');
const originalClientHeight = Object.getOwnPropertyDescriptor(HTMLElement.prototype, 'clientHeight');

afterEach(() => {
  cleanup();
  globalThis.ResizeObserver = originalResizeObserver;
  if (originalClientWidth) {
    Object.defineProperty(HTMLElement.prototype, 'clientWidth', originalClientWidth);
  } else {
    Reflect.deleteProperty(HTMLElement.prototype, 'clientWidth');
  }
  if (originalClientHeight) {
    Object.defineProperty(HTMLElement.prototype, 'clientHeight', originalClientHeight);
  } else {
    Reflect.deleteProperty(HTMLElement.prototype, 'clientHeight');
  }
});

describe('inherited people-counting video preview', () => {
  it('keeps natural video aspect ratio and maps clicks in the rendered video coordinate space', () => {
    globalThis.ResizeObserver = class {
      observe = vi.fn();

      disconnect = vi.fn();
    } as unknown as typeof ResizeObserver;
    Object.defineProperties(HTMLElement.prototype, {
      clientWidth: { configurable: true, get: () => 640 },
      clientHeight: { configurable: true, get: () => 360 },
    });
    const onPickPoint = vi.fn();
    render(
      <I18nWrapper>
        <VideoPreview
          config={{
            counter_name: 'door',
            target_class_name: 'person',
            line_x1_permille: 200,
            line_y1_permille: 300,
            line_x2_permille: 700,
            line_y2_permille: 800,
            outside_x_permille: 344,
            outside_y_permille: 656,
            conf_threshold_permille: 500,
            max_dist_permille: 100,
            track_history_k: 8,
            max_miss: 5,
            k_confirm: 3,
            window_minutes: 30,
            tracks_report_enable: false,
            heat_grid_enable: false,
          }}
          stats={null}
          editMode
          editPhase={0}
          onPickPoint={onPickPoint}
        />
      </I18nWrapper>
    );
    const stage = document.querySelector(
      '[data-testid="pc-video-stage"]'
    ) as HTMLDivElement;
    expect(stage).toBeTruthy();
    stage.getBoundingClientRect = () => ({
      left: 100,
      top: 50,
      width: 640,
      height: 360,
      right: 740,
      bottom: 410,
      x: 100,
      y: 50,
      toJSON: () => ({}),
    });
    fireEvent.click(stage, { clientX: 420, clientY: 230 });
    expect(onPickPoint).toHaveBeenCalledWith(500, 500);
    const video = document.getElementById('pcVideoPlayer') as HTMLVideoElement;
    Object.defineProperty(video, 'videoWidth', {
      configurable: true,
      value: 1920,
    });
    Object.defineProperty(video, 'videoHeight', {
      configurable: true,
      value: 1080,
    });
    fireEvent.loadedMetadata(video);
    expect(video.style.aspectRatio).toBe(String(1920 / 1080));
  });
});
