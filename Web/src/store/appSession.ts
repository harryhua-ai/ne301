import { create } from 'zustand'
import appSessionApi, {
  isV3LineCrossingCapable,
  LC_CONTROL_POLL_MS,
  LC_CONTROL_WAIT_TIMEOUT_MS,
  SNAPSHOT_STALE_MS,
  type AppSessionStartResponse,
  type AppSessionStatusResponse,
  type AppSessionStopResponse,
  type BusinessSnapshotResponse,
  type ControlStatusResponse,
  type LineCrossingSnapshotPayload,
  type ReportStatusResponse,
} from '@/services/api/appSession'
import type { ApiEnvelope } from '@/services/api/appManagement'

export interface SessionOutcome {
  action: 'start' | 'stop'
  kind: 'started' | 'stopped' | 'stopping' | 'refused' | 'auth' | 'envelope' | 'indeterminate'
  reason: string | null
  detail: string | null
}

export type ControlWatchState =
  | { phase: 'idle' }
  | { phase: 'submitting' }
  | { phase: 'watching'; requestId: number; msgType: 'config' | 'reset' }
  | {
      phase: 'terminal'
      requestId: number
      msgType: 'config' | 'reset'
      cmdState: string
      resultConfigGeneration: number | null
      resultStateRevision: number | null
      timedOut: boolean
    }
  | { phase: 'refused'; reason: string | null; statusText: string | null }

export interface AppSessionData {
  session: AppSessionStatusResponse | null
  sessionAuthBlocked: 'unauthorized' | 'forbidden' | null
  snapshot: BusinessSnapshotResponse | null
  snapshotPayload: LineCrossingSnapshotPayload | null
  reports: ReportStatusResponse | null
  control: ControlStatusResponse | null
  busy: boolean
  outcome: SessionOutcome | null
  watch: ControlWatchState
  capabilityAppId: string | null
}

interface AppSessionStore extends AppSessionData {
  refresh: () => Promise<void>
  refreshSnapshotAndReports: () => Promise<void>
  startSession: () => Promise<void>
  stopSession: () => Promise<void>
  submitConfig: (args: {
    requestId: number
    expectedConfigGeneration: number
    payloadBase64: string
    destructiveConfirmed: boolean
  }) => Promise<void>
  submitReset: (args: {
    requestId: number
    expectedConfigGeneration: number
    destructiveConfirmed: boolean
  }) => Promise<void>
  watchControlTerminal: (requestId: number, msgType: 'config' | 'reset') => Promise<void>
  clearOutcome: () => void
  resetWatch: () => void
}

function envelopeFrom(err: unknown): ApiEnvelope | null {
  const candidate = err as { data?: unknown; response?: { data?: unknown } }
  if (candidate && typeof candidate === 'object') {
    if (candidate.data && typeof candidate.data === 'object') return candidate.data as ApiEnvelope
    if (candidate.response && typeof candidate.response === 'object') {
      const inner = (candidate.response as { data?: unknown }).data
      if (inner && typeof inner === 'object') return inner as ApiEnvelope
    }
  }
  return null
}

function envelopeCode(err: unknown, fallback: string): string {
  const env = envelopeFrom(err)
  return env?.error_code || fallback
}

let watchToken = 0

const sleepMs = (ms: number) => new Promise<void>((resolve) => {
    setTimeout(resolve, ms)
  })

export const useAppSessionStore = create<AppSessionStore>((set, get) => ({
  session: null,
  sessionAuthBlocked: null,
  snapshot: null,
  snapshotPayload: null,
  reports: null,
  control: null,
  busy: false,
  outcome: null,
  watch: { phase: 'idle' },
  capabilityAppId: null,

  refresh: async () => {
    try {
      const res = (await appSessionApi.getSessionStatus()) as ApiEnvelope<AppSessionStatusResponse>
      if (!res || res.success === false) {
        const code = res?.error_code || 'UNKNOWN'
        set({
          session: null,
          sessionAuthBlocked:
            code === 'UNAUTHORIZED' ? 'unauthorized' : code === 'FORBIDDEN' ? 'forbidden' : null,
          capabilityAppId: null,
          snapshot: null,
          snapshotPayload: null,
          reports: null,
          control: null,
        })
        return
      }
      set({ session: res.data || {}, sessionAuthBlocked: null })
      await get().refreshSnapshotAndReports()
    } catch (err) {
      const code = envelopeCode(err, 'UNKNOWN')
      set({
        session: null,
        sessionAuthBlocked:
          code === 'UNAUTHORIZED' ? 'unauthorized' : code === 'FORBIDDEN' ? 'forbidden' : null,
        capabilityAppId: null,
        snapshot: null,
        snapshotPayload: null,
        reports: null,
        control: null,
      })
    }
  },

  refreshSnapshotAndReports: async () => {
    const { session } = get()
    if (!session || session.state !== 'running') {
      set({
        snapshot: null,
        snapshotPayload: null,
        reports: null,
        control: null,
        capabilityAppId: null,
      })
      return
    }
    let snapshot: BusinessSnapshotResponse | null = null
    let payload: LineCrossingSnapshotPayload | null = null
    try {
      const res = (await appSessionApi.getBusinessSnapshot()) as ApiEnvelope<BusinessSnapshotResponse>
      if (res && res.success !== false && res.data) {
        snapshot = res.data
        if (
          snapshot
          && snapshot.payload
          && typeof snapshot.payload === 'object'
          && !Array.isArray(snapshot.payload)
        ) {
          payload = snapshot.payload as LineCrossingSnapshotPayload
        }
      }
    } catch {
      snapshot = null
      payload = null
    }
    let reports: ReportStatusResponse | null = null
    try {
      const res = (await appSessionApi.getReportStatus()) as ApiEnvelope<ReportStatusResponse>
      if (res && res.success !== false && res.data) {
        reports = res.data
      }
    } catch {
      reports = null
    }
    try {
      const res = (await appSessionApi.getControlStatus()) as ApiEnvelope<ControlStatusResponse>
      if (res && res.success !== false && res.data) {
        set({ control: res.data })
      } else {
        set({ control: null })
      }
    } catch {
      set({ control: null })
    }
    const appId = typeof session.app_id === 'string' ? session.app_id : ''
    const capable = isV3LineCrossingCapable(session, snapshot)
    set({
      snapshot,
      snapshotPayload: payload,
      reports,
      capabilityAppId: capable && appId ? appId : null,
    })
  },

  startSession: async () => {
    if (get().busy) return
    set({ busy: true, outcome: null })
    try {
      const res = (await appSessionApi.startSession()) as ApiEnvelope<AppSessionStartResponse>
      if (!res || res.success === false) {
        const code = res?.error_code || 'UNKNOWN'
        set({
          outcome: {
            action: 'start',
            kind: code === 'UNAUTHORIZED' || code === 'FORBIDDEN' ? 'auth' : 'envelope',
            reason: code,
            detail: null,
          },
        })
        return
      }
      const d = res.data || {}
      if (d.status === 'started') {
        set({ outcome: { action: 'start', kind: 'started', reason: null, detail: null } })
      } else {
        set({
          outcome: {
            action: 'start',
            kind: 'refused',
            reason: d.reason || 'UNKNOWN',
            detail: d.detail || null,
          },
        })
      }
    } catch (err) {
      const code = envelopeCode(err, '')
      if (code) {
        set({
          outcome: {
            action: 'start',
            kind: code === 'UNAUTHORIZED' || code === 'FORBIDDEN' ? 'auth' : 'envelope',
            reason: code,
            detail: null,
          },
        })
      } else {
        set({ outcome: { action: 'start', kind: 'indeterminate', reason: null, detail: null } })
      }
    } finally {
      set({ busy: false })
      await get().refresh()
    }
  },

  stopSession: async () => {
    if (get().busy) return
    set({ busy: true, outcome: null })
    try {
      const res = (await appSessionApi.stopSession()) as ApiEnvelope<AppSessionStopResponse>
      if (!res || res.success === false) {
        const code = res?.error_code || 'UNKNOWN'
        set({
          outcome: {
            action: 'stop',
            kind: code === 'UNAUTHORIZED' || code === 'FORBIDDEN' ? 'auth' : 'envelope',
            reason: code,
            detail: null,
          },
        })
        return
      }
      const d = res.data || {}
      if (d.status === 'stopped') {
        set({ outcome: { action: 'stop', kind: 'stopped', reason: null, detail: null } })
      } else if (d.status === 'stopping') {
        set({
          outcome: {
            action: 'stop',
            kind: 'stopping',
            reason: d.busy === true ? 'BUSY' : d.reason || null,
            detail: null,
          },
        })
      } else {
        set({
          outcome: {
            action: 'stop',
            kind: 'refused',
            reason: d.reason || 'UNKNOWN',
            detail: null,
          },
        })
      }
    } catch (err) {
      const code = envelopeCode(err, '')
      if (code) {
        set({
          outcome: {
            action: 'stop',
            kind: code === 'UNAUTHORIZED' || code === 'FORBIDDEN' ? 'auth' : 'envelope',
            reason: code,
            detail: null,
          },
        })
      } else {
        set({ outcome: { action: 'stop', kind: 'indeterminate', reason: null, detail: null } })
      }
    } finally {
      set({ busy: false })
      await get().refresh()
    }
  },

  submitConfig: async ({ requestId, expectedConfigGeneration, payloadBase64, destructiveConfirmed }) => {
    if (get().watch.phase === 'submitting' || get().watch.phase === 'watching') return
    set({
      watch: { phase: 'submitting' },
      outcome: null,
    })
    try {
      const res = (await appSessionApi.submitConfig({
        request_id: requestId,
        expected_config_generation: expectedConfigGeneration,
        payload_base64: payloadBase64,
        destructive_confirmed: destructiveConfirmed,
      })) as ApiEnvelope<ControlStatusResponse & { status?: string; reason?: string }>
      if (!res || res.success === false) {
        const code = res?.error_code || 'UNKNOWN'
        set({
          watch: {
            phase: 'refused',
            reason: code,
            statusText: null,
          },
        })
        return
      }
      const d = res.data || {}
      if (d.status !== 'registered' && d.status !== 'cached' && d.status !== 'already_registered') {
        set({ watch: { phase: 'refused', reason: d.reason || null, statusText: d.status || null } })
        return
      }
      await get().watchControlTerminal(requestId, 'config')
    } catch (err) {
      const code = envelopeCode(err, 'UNKNOWN')
      set({ watch: { phase: 'refused', reason: code, statusText: null } })
    }
  },

  submitReset: async ({ requestId, expectedConfigGeneration, destructiveConfirmed }) => {
    if (get().watch.phase === 'submitting' || get().watch.phase === 'watching') return
    set({ watch: { phase: 'submitting' }, outcome: null })
    try {
      const res = (await appSessionApi.submitReset({
        request_id: requestId,
        expected_config_generation: expectedConfigGeneration,
        destructive_confirmed: destructiveConfirmed,
      })) as ApiEnvelope<ControlStatusResponse & { status?: string; reason?: string }>
      if (!res || res.success === false) {
        const code = res?.error_code || 'UNKNOWN'
        set({ watch: { phase: 'refused', reason: code, statusText: null } })
        return
      }
      const d = res.data || {}
      if (d.status !== 'registered' && d.status !== 'cached' && d.status !== 'already_registered') {
        set({ watch: { phase: 'refused', reason: d.reason || null, statusText: d.status || null } })
        return
      }
      await get().watchControlTerminal(requestId, 'reset')
    } catch (err) {
      const code = envelopeCode(err, 'UNKNOWN')
      set({ watch: { phase: 'refused', reason: code, statusText: null } })
    }
  },

  watchControlTerminal: async (requestId, msgType) => {
    const token = ++watchToken
    set({ watch: { phase: 'watching', requestId, msgType } })
    const deadline = Date.now() + LC_CONTROL_WAIT_TIMEOUT_MS
    const finishUnknown = async () => {
      set({
        watch: {
          phase: 'terminal',
          requestId,
          msgType,
          cmdState: 'unknown',
          resultConfigGeneration: null,
          resultStateRevision: null,
          timedOut: true,
        },
      })
      await get().refreshSnapshotAndReports()
    }
    const poll = async (): Promise<void> => {
      if (token !== watchToken) return undefined
      await sleepMs(LC_CONTROL_POLL_MS)
      if (token !== watchToken) return undefined
      try {
        const res = (await appSessionApi.getControlStatus()) as ApiEnvelope<ControlStatusResponse>
        if (res && res.success !== false && res.data) {
          const cmd = res.data.command
          set({ control: res.data })
          const state = cmd ? String(cmd.state || 'unknown') : 'unknown'
          const isPending = state === 'pending' || state === 'delivered' || state === 'none'
          const matches = cmd !== undefined && cmd !== null && cmd.request_id === requestId
          if (matches && !isPending) {
            set({
              watch: {
                phase: 'terminal',
                requestId,
                msgType,
                cmdState: state,
                resultConfigGeneration:
                  typeof cmd.result_config_generation === 'number'
                    ? cmd.result_config_generation
                    : null,
                resultStateRevision:
                  typeof cmd.result_state_revision === 'number' ? cmd.result_state_revision : null,
                timedOut: false,
              },
            })
            await get().refreshSnapshotAndReports()
            return undefined
          }
        }
      } catch {
        set({
          watch: { phase: 'watching', requestId, msgType },
        })
      }
      if (Date.now() > deadline) {
        await finishUnknown()
        return undefined
      }
      return poll()
    }
    await poll()
  },

  clearOutcome: () => set({ outcome: null }),

  resetWatch: () => set({ watch: { phase: 'idle' } }),
}))

export { SNAPSHOT_STALE_MS }
