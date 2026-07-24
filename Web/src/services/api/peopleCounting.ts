import request from '../request';

/**
 * People Counting config — mirrors people_counting_config_t from the firmware.
 * All permille fields are 0-1000 (1000 = 1.0 normalized = 100% of dimension).
 */
export interface PeopleCountingConfig {
    enable: boolean;
    line_x1_permille: number;
    line_y1_permille: number;
    line_x2_permille: number;
    line_y2_permille: number;
    outside_x_permille: number;
    outside_y_permille: number;
    conf_threshold_permille: number;
    max_dist_permille: number;
    target_class_name: string;
    model_name: string;
    model_pp_type: string;
    track_history_k: number;
    max_miss: number;
    k_confirm: number;
    window_minutes: number;
    mqtt_report_enable: boolean;
    webhook_report_enable: boolean;
    tracks_report_enable: boolean;
    heat_grid_enable: boolean;
    backlog_capacity: number;
}

export interface PeopleCountingStats {
    window_in: number;
    window_out: number;
    window_start_ts: number;
    total_in: number;
    total_out: number;
    boot_id: number;
    boot_id_kind: string;
    last_report_ts: number;
    dropped_windows_mqtt: number;
    dropped_windows_webhook: number;
    heat_grid: number[];
    /** Debug counters (diagnostics) — emitted by firmware, optional for older builds. */
    dbg_sub_calls?: number;
    dbg_matched_person?: number;
    dbg_last_type?: number;
    dbg_last_nb_detect?: number;
    dbg_last_class?: string;
    dbg_last_det_x?: number;
    dbg_last_det_y?: number;
}

export interface BacklogInfo {
    mqtt: { count: number };
    webhook: { count: number };
}

const BASE = '/api/v1/apps/people-counting';

const peopleCounting = {
    getConfig: () => request.get(`${BASE}/config`),
    setConfig: (data: Partial<PeopleCountingConfig>) => request.post(`${BASE}/config`, data),
    getStats: () => request.get(`${BASE}/stats`),
    resetTotals: () => request.post(`${BASE}/reset`),
    getBacklog: () => request.get(`${BASE}/backlog`),
};

export default peopleCounting;
