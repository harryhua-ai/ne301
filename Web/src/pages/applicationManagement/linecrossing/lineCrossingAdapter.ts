import type {
  LineCrossingSnapshotPayload,
  LineConfigCandidateInput,
} from '@/services/api/appSession';

export interface LineCrossingEditorConfig {
  counter_name: string;
  target_class_name: string;
  line_x1_permille: number;
  line_y1_permille: number;
  line_x2_permille: number;
  line_y2_permille: number;
  outside_x_permille: number;
  outside_y_permille: number;
  conf_threshold_permille: number;
  max_dist_permille: number;
  track_history_k: number;
  max_miss: number;
  k_confirm: number;
  window_minutes: number;
  tracks_report_enable: boolean;
  heat_grid_enable: boolean;
}

export interface LineCrossingStatsView {
  window_in: number | null;
  window_out: number | null;
  total_in: number | null;
  total_out: number | null;
  report_seq: number | null;
  gap_gaps: number | null;
  gap_lost: number | null;
  gap_lost_unknown: number | null;
}

export function configFromSnapshot(
  payload: LineCrossingSnapshotPayload | null | undefined
): LineCrossingEditorConfig | null {
  const active = payload?.active_config;
  if (!active) return null;
  return {
    counter_name:
      typeof active.counter_name === 'string' ? active.counter_name : '',
    target_class_name:
      typeof active.target_class_name === 'string'
        ? active.target_class_name
        : '',
    line_x1_permille: active.line?.x1_permille ?? 0,
    line_y1_permille: active.line?.y1_permille ?? 0,
    line_x2_permille: active.line?.x2_permille ?? 0,
    line_y2_permille: active.line?.y2_permille ?? 0,
    outside_x_permille: active.outside?.x_permille ?? 0,
    outside_y_permille: active.outside?.y_permille ?? 0,
    conf_threshold_permille: active.conf_threshold_permille ?? 500,
    max_dist_permille: active.max_dist_permille ?? 100,
    track_history_k: active.track_history_k ?? 8,
    max_miss: active.max_miss ?? 5,
    k_confirm: active.k_confirm ?? 3,
    window_minutes: active.window_minutes ?? 30,
    tracks_report_enable: active.tracks_report_enable === true,
    heat_grid_enable: active.heat_grid_enable === true,
  };
}

export function candidateFromEditorConfig(
  config: LineCrossingEditorConfig
): LineConfigCandidateInput {
  return {
    counter_name: config.counter_name,
    target_class_name: config.target_class_name,
    line: {
      x1: config.line_x1_permille,
      y1: config.line_y1_permille,
      x2: config.line_x2_permille,
      y2: config.line_y2_permille,
    },
    outside: { x: config.outside_x_permille, y: config.outside_y_permille },
    conf_threshold_permille: config.conf_threshold_permille,
    max_dist_permille: config.max_dist_permille,
    track_history_k: config.track_history_k,
    max_miss: config.max_miss,
    k_confirm: config.k_confirm,
    window_minutes: config.window_minutes,
    tracks_report_enable: config.tracks_report_enable,
    heat_grid_enable: config.heat_grid_enable,
  };
}

export function statsFromSnapshot(
  payload: LineCrossingSnapshotPayload | null | undefined
): LineCrossingStatsView | null {
  if (!payload) return null;
  return {
    window_in: payload.counters?.in ?? null,
    window_out: payload.counters?.out ?? null,
    total_in: payload.cumulative?.in ?? null,
    total_out: payload.cumulative?.out ?? null,
    report_seq: payload.report_seq ?? null,
    gap_gaps: payload.gap?.gaps ?? null,
    gap_lost: payload.gap?.lost ?? null,
    gap_lost_unknown: payload.gap?.lost_unknown ?? null,
  };
}
