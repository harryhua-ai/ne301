import request from '../request'

export interface AppSessionModelInfo {
  model_name?: string
  model_version?: string
  model_generation?: number
  class_generation?: number
  class_count?: number
}

export interface AppSessionCounters {
  events_delivered?: number
  frames_dropped?: number
  gap_events?: number
  model_changes?: number
}

export interface AppSessionStatusResponse {
  state?: 'idle' | 'running' | 'stopping' | string
  publisher_id?: string
  app_id?: string
  version_major?: number
  version_minor?: number
  version_patch?: number
  app_running?: boolean
  abi_version?: number
  granted_caps?: string
  event_max?: number
  model?: AppSessionModelInfo
  counters?: AppSessionCounters
  last_result?: string
  last_reason?: string
}

export interface AppSessionStartResponse {
  status?: 'started' | 'refused' | string
  publisher_id?: string
  app_id?: string
  version_major?: number
  version_minor?: number
  version_patch?: number
  abi_version?: number
  granted_caps?: string
  event_max?: number
  app_running?: boolean
  reason?: string
  detail?: string
  executable?: boolean
}

export interface AppSessionStopResponse {
  status?: 'stopped' | 'stopping' | 'refused' | string
  entry_returned?: boolean
  busy?: boolean
  reason?: string
  log?: string
  log_truncated?: boolean
  app_running?: boolean
}

export type ControlCmdState =
  | 'none'
  | 'pending'
  | 'delivered'
  | 'applied'
  | 'rejected_stale_config'
  | 'rejected_invalid'
  | 'rejected_resource'
  | 'rejected_unconfirmed'
  | 'failed_state_conflict'
  | 'failed_storage_io'
  | 'failed_storage_unknown'
  | 'unknown'
  | string

export interface ControlCommandView {
  state?: ControlCmdState
  request_id?: number
  msg_type?: string
  expected_config_generation?: number
  flags?: number
  payload_len?: number
  result_config_generation?: number
  result_state_revision?: number
}

export interface ControlStatusResponse {
  session_active?: boolean
  session_generation?: number
  control_max?: number
  snapshot_max?: number
  known_config_generation?: number
  command?: ControlCommandView
  last_unknown?: {
    count?: number
    session_generation?: number
    request_id?: number
    msg_type?: string
  }
}

export type SnapshotAvailability = 'fresh' | 'stale' | 'unavailable' | string

export interface BusinessSnapshotResponse {
  availability?: SnapshotAvailability
  session_generation?: number
  visible_version?: number
  config_generation?: number
  state_revision?: number
  age_ms?: number
  payload_len?: number
  publisher_id?: string
  app_id?: string
  payload?: unknown
}

export interface LineCrossingSnapshotPayload {
  schema?: string
  config_generation?: number
  state_revision?: number
  app_state?: 'running' | 'unbound' | 'restricted' | string
  storage?: { state?: string; withheld?: boolean }
  report_seq?: number
  cumulative?: { in?: number; out?: number }
  counters?: { in?: number; out?: number }
  gap?: { gaps?: number; lost?: number; lost_unknown?: number; unusable?: number }
  model?: { valid?: boolean; generation?: number; class_generation?: number; target_class?: string }
  active_config?: LineCrossingActiveConfig
}

export interface LineCrossingActiveConfig {
  counter_name?: string
  target_class_name?: string
  line?: { x1_permille?: number; y1_permille?: number; x2_permille?: number; y2_permille?: number }
  outside?: { x_permille?: number; y_permille?: number }
  conf_threshold_permille?: number
  max_dist_permille?: number
  track_history_k?: number
  max_miss?: number
  k_confirm?: number
  window_minutes?: number
  tracks_report_enable?: boolean
  heat_grid_enable?: boolean
}

export interface ReportEntryView {
  report_seq?: number
  mqtt_state?: string
  webhook_state?: string
  mqtt_attempts?: number
  webhook_attempts?: number
  payload_len?: number
}

export interface ReportStatusResponse {
  session_bound?: boolean
  session_state?: string
  publisher_id?: string
  app_id?: string
  host_boot_id?: number
  queue_slots?: number
  queue_used?: number
  reports?: ReportEntryView[]
}

const quietConfig = { skipErrorToast: true } as any

const appSessionApi = {
  getSessionStatus: () => request.get('/api/v1/app/session/status', quietConfig),
  startSession: () => request.post('/api/v1/app/session/start', {}, quietConfig),
  stopSession: () => request.post('/api/v1/app/session/stop', {}, quietConfig),
  getControlStatus: () => request.get('/api/v1/app/control/status', quietConfig),
  submitConfig: (body: {
    request_id: number
    expected_config_generation: number
    payload_base64: string
    destructive_confirmed?: boolean
  }) => request.post('/api/v1/app/control/config', body, quietConfig),
  submitReset: (body: { request_id: number; expected_config_generation: number; destructive_confirmed: boolean }) => request.post('/api/v1/app/control/reset', body, quietConfig),
  getBusinessSnapshot: () => request.get('/api/v1/app/business/snapshot', quietConfig),
  getReportStatus: () => request.get('/api/v1/app/report/status', quietConfig),
}

export const APP_ABI_VERSION_V3 = 0x00030000
export const APP_CAP_REPORT_SUBMIT = 0x00000008
export const APP_CAP_CONTROL_RECEIVE = 0x00000040
export const APP_CAP_BUSINESS_SNAPSHOT = 0x00000080
export const LC_CONFIG_CANDIDATE_SIZE = 144
export const LC_CONFIG_SCHEMA_VERSION = 1
export const LC_CONFIG_MAGIC = 0x3343434c
export const LC_SNAPSHOT_SCHEMA = 'line-crossing/1'
export const LC_CONTROL_WAIT_TIMEOUT_MS = 30000
export const LC_CONTROL_POLL_MS = 1000
export const SNAPSHOT_STALE_MS = 10000

export function parseCapsHex(caps: string | undefined): number {
  if (typeof caps !== 'string') return 0
  const m = caps.match(/^0x([0-9a-fA-F]+)$/)
  if (!m) return 0
  const v = Number.parseInt(m[1], 16)
  if (!Number.isFinite(v) || v < 0 || v > 0xffffffff) return 0
  return v >>> 0
}

export function newRequestId(): number {
  const buf = new Uint32Array(1)
  if (typeof crypto !== 'undefined' && typeof crypto.getRandomValues === 'function') {
    crypto.getRandomValues(buf)
  } else {
    buf[0] = Math.floor(Math.random() * 0xffffffff)
  }
  let v = buf[0] >>> 0
  if (v === 0) v = 1
  return v
}

function encodeUtf8(text: string): Uint8Array {
  if (typeof TextEncoder !== 'undefined') {
    return new TextEncoder().encode(text)
  }
  const out: number[] = []
  for (let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i)
    if (c < 0x80) out.push(c)
    else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 0x3f))
    else out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 0x3f), 0x80 | (c & 0x3f))
  }
  return new Uint8Array(out)
}

function decodeUtf8(bytes: Uint8Array): string {
  if (typeof TextDecoder !== 'undefined') {
    return new TextDecoder('utf-8').decode(bytes)
  }
  let out = ''
  for (let i = 0; i < bytes.length; i++) {
    out += String.fromCharCode(bytes[i])
  }
  return out
}

export interface LineConfigCandidateInput {
  counter_name: string
  target_class_name: string
  line: { x1: number; y1: number; x2: number; y2: number }
  outside: { x: number; y: number }
  conf_threshold_permille: number
  max_dist_permille: number
  track_history_k: number
  max_miss: number
  k_confirm: number
  window_minutes: number
  tracks_report_enable: boolean
  heat_grid_enable: boolean
}

function validUtf8Name(text: string, maxBytes: number): boolean {
  if (typeof text !== 'string' || text.length === 0) return false
  const bytes = encodeUtf8(text)
  if (bytes.length > maxBytes) return false
  for (const b of bytes) {
    if (b === 0x22 || b === 0x5c) return false
    if (b < 0x20 || (b >= 0x7f && b <= 0x9f)) return false
  }
  const round = decodeUtf8(encodeUtf8(text))
  if (round !== text) return false
  return true
}

export function validateLineConfigCandidate(
  c: LineConfigCandidateInput,
): { ok: boolean; reason?: string } {
  const permille = (v: number) => Number.isInteger(v) && v >= 0 && v <= 1000
  if (!validUtf8Name(c.target_class_name, 31)) return { ok: false, reason: 'target_class_name' }
  if (!validUtf8Name(c.counter_name, 63)) return { ok: false, reason: 'counter_name' }
  if (![c.line.x1, c.line.y1, c.line.x2, c.line.y2, c.outside.x, c.outside.y].every(permille)) {
    return { ok: false, reason: 'line' }
  }
  if (c.line.x1 === c.line.x2 && c.line.y1 === c.line.y2) return { ok: false, reason: 'line' }
  if (!permille(c.conf_threshold_permille)) return { ok: false, reason: 'conf_threshold_permille' }
  if (!Number.isInteger(c.max_dist_permille) || c.max_dist_permille < 1 || c.max_dist_permille > 1000) {
    return { ok: false, reason: 'max_dist_permille' }
  }
  if (!Number.isInteger(c.track_history_k) || c.track_history_k < 4 || c.track_history_k > 16) {
    return { ok: false, reason: 'track_history_k' }
  }
  if (!Number.isInteger(c.max_miss) || c.max_miss < 1) return { ok: false, reason: 'max_miss' }
  if (!Number.isInteger(c.k_confirm) || c.k_confirm < 1) return { ok: false, reason: 'k_confirm' }
  if (typeof c.tracks_report_enable !== 'boolean' || typeof c.heat_grid_enable !== 'boolean') {
    return { ok: false, reason: 'toggles' }
  }
  if (!Number.isInteger(c.window_minutes) || c.window_minutes < 1 || c.window_minutes > 1440) {
    return { ok: false, reason: 'window_minutes' }
  }
  return { ok: true }
}

export function encodeLineConfigCandidate(c: LineConfigCandidateInput): Uint8Array {
  const buf = new ArrayBuffer(LC_CONFIG_CANDIDATE_SIZE)
  const dv = new DataView(buf)
  const u8 = new Uint8Array(buf)
  dv.setUint32(0, LC_CONFIG_MAGIC, true)
  dv.setUint32(4, LC_CONFIG_SCHEMA_VERSION, true)
  dv.setUint32(8, LC_CONFIG_CANDIDATE_SIZE, true)
  dv.setUint32(12, 0, true)
  const targetBytes = encodeUtf8(c.target_class_name)
  u8[16] = targetBytes.length
  u8.set(targetBytes, 17)
  const counterBytes = encodeUtf8(c.counter_name)
  u8[49] = counterBytes.length
  u8.set(counterBytes, 50)
  dv.setUint16(114, c.line.x1, true)
  dv.setUint16(116, c.line.y1, true)
  dv.setUint16(118, c.line.x2, true)
  dv.setUint16(120, c.line.y2, true)
  dv.setUint16(122, c.outside.x, true)
  dv.setUint16(124, c.outside.y, true)
  dv.setUint16(126, c.conf_threshold_permille, true)
  dv.setUint16(128, c.max_dist_permille, true)
  u8[130] = c.track_history_k
  u8[131] = c.max_miss
  u8[132] = c.k_confirm
  u8[133] = (c.tracks_report_enable ? 1 : 0) | (c.heat_grid_enable ? 2 : 0)
  dv.setUint16(134, c.window_minutes, true)
  return u8
}

export function candidateToBase64(bytes: Uint8Array): string {
  let bin = ''
  for (const b of bytes) bin += String.fromCharCode(b)
  if (typeof btoa === 'function') return btoa(bin)
  if (typeof Buffer !== 'undefined') return Buffer.from(bytes).toString('base64')
  throw new Error('no base64 encoder')
}

export function base64ToBytes(b64: string): Uint8Array {
  if (typeof atob === 'function') {
    const bin = atob(b64)
    const out = new Uint8Array(bin.length)
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i)
    return out
  }
  if (typeof Buffer !== 'undefined') return new Uint8Array(Buffer.from(b64, 'base64'))
  throw new Error('no base64 decoder')
}

export { encodeUtf8, decodeUtf8 }

export function isV3LineCrossingCapable(
  session: AppSessionStatusResponse | null | undefined,
  snapshot: BusinessSnapshotResponse | null | undefined,
): boolean {
  if (!session || session.state !== 'running') return false
  if (session.abi_version !== APP_ABI_VERSION_V3) return false
  const caps = parseCapsHex(session.granted_caps)
  if ((caps & APP_CAP_REPORT_SUBMIT) === 0) return false
  if ((caps & APP_CAP_CONTROL_RECEIVE) === 0) return false
  if ((caps & APP_CAP_BUSINESS_SNAPSHOT) === 0) return false
  if (!snapshot || !snapshot.payload || typeof snapshot.payload !== 'object') return false
  const payload = snapshot.payload as LineCrossingSnapshotPayload
  if (payload.schema !== LC_SNAPSHOT_SCHEMA) return false
  return true
}

export default appSessionApi
