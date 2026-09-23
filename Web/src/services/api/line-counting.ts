import request from '../request';

export type LineCountingState =
    | 'disabled'
    | 'running'
    | 'unsupported_model'
    | 'target_class_invalid';

export type LineCountingReason =
    | 'model_not_loaded'
    | 'class_metadata_unavailable'
    | null;

export interface LineCountingLine {
    x1: number;
    y1: number;
    x2: number;
    y2: number;
    outside_x: number;
    outside_y: number;
}

export interface LineCountingTracking {
    association_distance: number;
    history_length: number;
    max_missed_frames: number;
    confirmation_frames: number;
}

export interface LineCountingReporting {
    mqtt_enabled: boolean;
    webhook_enabled: boolean;
    tracks_enabled: boolean;
    heat_grid_enabled: boolean;
    backlog_capacity: number;
}

export interface LineCountingConfig {
    enable: boolean;
    counter_name: string;
    target_class: string;
    line: LineCountingLine;
    confidence_threshold: number;
    tracking: LineCountingTracking;
    window_minutes: number;
    reporting: LineCountingReporting;
}

export interface LineCountingModelClass {
    id: number;
    name: string;
}

export interface LineCountingModel {
    name: string;
    version: string;
    postprocess_type: string;
    generation: number;
    classes: LineCountingModelClass[];
}

export interface LineCountingStatus {
    enabled: boolean;
    state: LineCountingState;
    reason: LineCountingReason;
    target_class: string;
    resetting?: boolean;
    model: LineCountingModel;
}

export interface LineCountingStats {
    window: { in: number; out: number };
    total: { in: number; out: number };
    delivery: {
        mqtt: { backlog: number; dropped: number };
        webhook: { backlog: number; dropped: number };
    };
}

export interface LineCountingEvent {
    sequence: number;
    timestamp_ms: number;
    track_id: number;
    direction: 'in' | 'out';
}

export interface LineCountingEvents {
    events: LineCountingEvent[];
    server_now_ms?: number;
}

export type LineTrackPoint = [number, number, number];

export interface LineTrack {
    track_id: number;
    points: LineTrackPoint[];
}

const BASE = '/api/v1/apps/line-counting';

export const normToPm = (v: number) => Math.round(v * 1000);
const pmToNorm = (v: number) => v / 1000;

function lineWireToUi(line: LineCountingLine): LineCountingLine {
    return {
        x1: normToPm(line.x1),
        y1: normToPm(line.y1),
        x2: normToPm(line.x2),
        y2: normToPm(line.y2),
        outside_x: normToPm(line.outside_x),
        outside_y: normToPm(line.outside_y),
    };
}

function lineUiToWire(line: LineCountingLine): LineCountingLine {
    return {
        x1: pmToNorm(line.x1),
        y1: pmToNorm(line.y1),
        x2: pmToNorm(line.x2),
        y2: pmToNorm(line.y2),
        outside_x: pmToNorm(line.outside_x),
        outside_y: pmToNorm(line.outside_y),
    };
}

const lineCounting = {
    getConfig: async () => {
        const res = await request.get(`${BASE}/config`);
        if (res.data?.line) res.data = { ...res.data, line: lineWireToUi(res.data.line) };
        return res;
    },
    setConfig: (data: LineCountingConfig) => request.post(`${BASE}/config`, { ...data, line: lineUiToWire(data.line) }, { skipErrorToast: true } as never),
    getStatus: () => request.get(`${BASE}/status`),
    getStats: () => request.get(`${BASE}/stats`),
    getEvents: () => request.get(`${BASE}/events`),
    getTracks: () => request.get(`${BASE}/tracks`, { skipErrorToast: true } as never),
    reset: () => request.post(`${BASE}/reset`, {}),
    isResetting: async () => {
        const res = await request.get(`${BASE}/status`);
        return !!(res.data && res.data.resetting);
    },
};

export default lineCounting;
