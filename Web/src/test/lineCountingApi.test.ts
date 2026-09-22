import { describe, it, expect, vi, beforeEach } from 'vitest';

import request from '../services/request';
import type { Mock } from 'vitest';
import lineCounting from '../services/api/line-counting';

vi.mock('../services/request', () => ({
  default: {
    get: vi.fn().mockResolvedValue({ data: {} }),
    post: vi.fn().mockResolvedValue({ data: {} }),
  },
}));

const mockedRequest = {
  get: request.get as unknown as Mock,
  post: request.post as unknown as Mock,
};

describe('lineCounting API client', () => {
  beforeEach(() => {
    mockedRequest.get.mockClear();
    mockedRequest.post.mockClear();
    mockedRequest.get.mockResolvedValue({ data: {} });
    mockedRequest.post.mockResolvedValue({ data: {} });
  });

  it('hits exact canonical paths', async () => {
    await lineCounting.getConfig();
    expect(mockedRequest.get).toHaveBeenCalledWith('/api/v1/apps/line-counting/config');
    await lineCounting.getStatus();
    expect(mockedRequest.get).toHaveBeenCalledWith('/api/v1/apps/line-counting/status');
    await lineCounting.getStats();
    expect(mockedRequest.get).toHaveBeenCalledWith('/api/v1/apps/line-counting/stats');
    await lineCounting.getEvents();
    expect(mockedRequest.get).toHaveBeenCalledWith('/api/v1/apps/line-counting/events');
  });

  it('save converts permille UI line coords to normalized wire coords', async () => {
    const cfg = {
      enable: true,
      counter_name: '客流统计',
      target_class: 'person',
      line: { x1: 200, y1: 500, x2: 800, y2: 500, outside_x: 500, outside_y: 200 },
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
    await lineCounting.setConfig(cfg);
    expect(mockedRequest.post).toHaveBeenCalledTimes(1);
    const [path, body] = mockedRequest.post.mock.calls[0];
    expect(path).toBe('/api/v1/apps/line-counting/config');
    expect(body.line).toEqual({ x1: 0.2, y1: 0.5, x2: 0.8, y2: 0.5, outside_x: 0.5, outside_y: 0.2 });
    expect(body.counter_name).toBe('客流统计');
    expect(body.window_minutes).toBe(5);
    expect(JSON.stringify(body)).not.toContain('permille');
  });

  it('retries save on transient UNAUTHORIZED business errors', async () => {
    const cfg = {
      enable: true,
      counter_name: 'door_main',
      target_class: 'person',
      line: { x1: 300, y1: 500, x2: 700, y2: 500, outside_x: 500, outside_y: 350 },
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
    mockedRequest.post
      .mockRejectedValueOnce({ data: { error_code: 'UNAUTHORIZED' } })
      .mockRejectedValueOnce({ data: { error_code: 'UNAUTHORIZED' } })
      .mockResolvedValueOnce({ data: { success: true } });
    vi.useFakeTimers();
    const p = lineCounting.setConfig(cfg);
    await vi.advanceTimersByTimeAsync(5000);
    await p;
    vi.useRealTimers();
    expect(mockedRequest.post).toHaveBeenCalledTimes(3);
  });

  it('does not retry save on non-transient errors', async () => {
    const cfg = {
      enable: true,
      counter_name: 'door_main',
      target_class: 'person',
      line: { x1: 300, y1: 500, x2: 700, y2: 500, outside_x: 500, outside_y: 350 },
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
    mockedRequest.post.mockRejectedValueOnce({ data: { error_code: 'INVALID_PARAM' } });
    await expect(lineCounting.setConfig(cfg)).rejects.toBeDefined();
    expect(mockedRequest.post).toHaveBeenCalledTimes(1);
  });

  it('getConfig converts wire line coords to permille UI coords', async () => {
    mockedRequest.get.mockResolvedValue({
      data: { counter_name: 'door_main', line: { x1: 0.2, y1: 0.5, x2: 0.8, y2: 0.5, outside_x: 0.5, outside_y: 0.2 } },
    });
    const res = await lineCounting.getConfig();
    expect(res.data.line).toEqual({ x1: 200, y1: 500, x2: 800, y2: 500, outside_x: 500, outside_y: 200 });
  });

  it('reset posts an explicit empty JSON body so the device receives Content-Length', async () => {
    await lineCounting.reset();
    expect(mockedRequest.post).toHaveBeenCalledTimes(1);
    const [path, body] = mockedRequest.post.mock.calls[0];
    expect(path).toBe('/api/v1/apps/line-counting/reset');
    expect(body).toEqual({});
  });
});
