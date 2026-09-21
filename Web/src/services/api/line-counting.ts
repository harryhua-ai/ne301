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
}

const BASE = '/api/v1/apps/line-counting';

const lineCounting = {
    getConfig: () => request.get(`${BASE}/config`),
    setConfig: (data: LineCountingConfig) => request.post(`${BASE}/config`, data, { skipErrorToast: true } as never),
    getStatus: () => request.get(`${BASE}/status`),
    getStats: () => request.get(`${BASE}/stats`),
    getEvents: () => request.get(`${BASE}/events`),
    reset: () => request.post(`${BASE}/reset`),
};

export default lineCounting;
