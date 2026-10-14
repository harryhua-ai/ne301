import { useEffect, useMemo, useState } from 'preact/hooks'
import { useLingui } from '@lingui/react'
import { Button } from '@/components/ui/button'
import { Card, CardContent } from '@/components/ui/card'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'
import { Switch } from '@/components/ui/switch'
import type { AppRecord } from '@/store/appManagement'
import { useAppSessionStore } from '@/store/appSession'
import {
  candidateToBase64,
  encodeLineConfigCandidate,
  LC_SNAPSHOT_SCHEMA,
  newRequestId,
  SNAPSHOT_STALE_MS,
  validateLineConfigCandidate,
  type LineConfigCandidateInput,
} from '@/services/api/appSession'
import VideoLineCanvas, {
  draftToLineFields,
  type LineDraft,
} from './VideoLineCanvas'

type InnerTab = 'live' | 'config' | 'advanced'

const REPORT_STATE_KEYS: Record<string, string> = {
  not_configured: 'sys.app_management.report.state.not_configured',
  pending: 'sys.app_management.report.state.pending',
  succeeded: 'sys.app_management.report.state.succeeded',
  failed: 'sys.app_management.report.state.failed',
  unknown: 'sys.app_management.report.state.unknown',
}

function reportStateKey(state: string | undefined): string {
  const normalized = String(state || '').toLowerCase()
  return REPORT_STATE_KEYS[normalized] || REPORT_STATE_KEYS.unknown
}

function StatRow({ label, value, testId }: { label: string; value: string; testId?: string }) {
  return (
    <div className="flex items-center justify-between gap-3 border-b border-[#eef1f4] py-1.5 text-sm last:border-b-0">
      <span className="shrink-0 text-[#718093]">{label}</span>
      <span className="break-all text-right font-medium text-[#2c3848]" data-testid={testId}>
        {value}
      </span>
    </div>
  )
}

function StatBig({
  value,
  label,
  tone,
  testId,
}: {
  value: string
  label: string
  tone: 'green' | 'red' | 'gray'
  testId: string
}) {
  const color = tone === 'green' ? 'text-green-600' : tone === 'red' ? 'text-red-600' : 'text-[#8994a5]'
  return (
    <div className="flex items-baseline gap-1.5 px-1.5 py-0.5">
      <div className={`text-xl font-bold leading-none ${color}`} data-testid={testId}>
        {value}
      </div>
      <div className="text-[10px] leading-tight text-[#718093]">{label}</div>
    </div>
  )
}

function WatchBanner() {
  const { i18n } = useLingui()
  const watch = useAppSessionStore((s) => s.watch)
  const snapshot = useAppSessionStore((s) => s.snapshot)
  const resetWatch = useAppSessionStore((s) => s.resetWatch)

  if (watch.phase === 'idle') return null
  if (watch.phase === 'submitting') {
    return (
      <div className="mt-2 rounded border border-[#e6eaee] bg-[#f7f9fb] p-2 text-sm text-[#334354]" data-testid="lc-watch-submitting">
        {i18n._('sys.app_management.receipt.submitting')}
      </div>
    )
  }
  if (watch.phase === 'watching') {
    return (
      <div className="mt-2 rounded border border-[#e6eaee] bg-[#f7f9fb] p-2 text-sm text-[#334354]" data-testid="lc-watch-pending">
        {i18n._('sys.app_management.receipt.waiting_receipt')}
      </div>
    )
  }
  if (watch.phase === 'refused') {
    return (
      <div className="mt-2 rounded border border-red-300 bg-red-50 p-2 text-sm text-red-700" data-testid="lc-watch-refused">
        <p>{i18n._('sys.app_management.receipt.refused')}</p>
        {watch.reason && <p className="mt-1 font-mono text-xs">{watch.reason}</p>}
        <Button variant="outline" size="sm" className="mt-2" onClick={resetWatch} data-testid="lc-watch-dismiss">
          {i18n._('sys.app_management.receipt.dismiss')}
        </Button>
      </div>
    )
  }
  const state = watch.cmdState
  if (state === 'applied' && !watch.timedOut) {
    return (
      <div className="mt-2 rounded border border-green-300 bg-green-50 p-2 text-sm text-green-800" data-testid="lc-watch-applied">
        {i18n._('sys.app_management.receipt.applied')}
        {watch.resultConfigGeneration !== null && (
          <span className="ml-1 font-mono text-xs">
            config_generation={watch.resultConfigGeneration}
          </span>
        )}
      </div>
    )
  }
  if (watch.timedOut || state === 'unknown') {
    return (
      <div className="mt-2 rounded border border-amber-300 bg-amber-50 p-2 text-sm text-amber-800" data-testid="lc-watch-unknown">
        <p>{i18n._('sys.app_management.receipt.unknown')}</p>
        <p className="mt-1 text-xs">
          {i18n._('sys.app_management.receipt.unknown_hint')}
          {typeof snapshot?.config_generation === 'number' && (
            <span className="ml-1 font-mono">
              (config_generation={snapshot.config_generation})
            </span>
          )}
        </p>
        <Button variant="outline" size="sm" className="mt-2" onClick={resetWatch} data-testid="lc-watch-dismiss">
          {i18n._('sys.app_management.receipt.dismiss')}
        </Button>
      </div>
    )
  }
  const key = `sys.app_management.receipt.state.${state}`
  return (
    <div className="mt-2 rounded border border-red-300 bg-red-50 p-2 text-sm text-red-700" data-testid="lc-watch-terminal">
      <p>{i18n._(key)}</p>
      {watch.resultConfigGeneration !== null && (
        <p className="mt-1 font-mono text-xs">config_generation={watch.resultConfigGeneration}</p>
      )}
      <Button variant="outline" size="sm" className="mt-2" onClick={resetWatch} data-testid="lc-watch-dismiss">
        {i18n._('sys.app_management.receipt.dismiss')}
      </Button>
    </div>
  )
}

export default function LineCrossingPage({ record }: { record: AppRecord }) {
  const { i18n } = useLingui()
  const session = useAppSessionStore((s) => s.session)
  const snapshot = useAppSessionStore((s) => s.snapshot)
  const payload = useAppSessionStore((s) => s.snapshotPayload)
  const reports = useAppSessionStore((s) => s.reports)
  const refreshSnapshotAndReports = useAppSessionStore((s) => s.refreshSnapshotAndReports)
  const submitConfig = useAppSessionStore((s) => s.submitConfig)
  const submitReset = useAppSessionStore((s) => s.submitReset)

  const [innerTab, setInnerTab] = useState<InnerTab>('live')
  const [draft, setDraft] = useState<LineConfigCandidateInput | null>(null)
  const [draftBaseGeneration, setDraftBaseGeneration] = useState<number | null>(null)
  const [destructiveAck, setDestructiveAck] = useState(false)
  const [resetConfirmOpen, setResetConfirmOpen] = useState(false)

  useEffect(() => {
    refreshSnapshotAndReports()
    const timer = window.setInterval(() => {
      refreshSnapshotAndReports()
    }, 5000)
    return () => window.clearInterval(timer)
  }, [refreshSnapshotAndReports])

  useEffect(() => {
    if (draft !== null) return
    const active = payload?.active_config
    if (!payload || !active) return
    const candidate: LineConfigCandidateInput = {
      counter_name: typeof active.counter_name === 'string' ? active.counter_name : '',
      target_class_name:
        typeof active.target_class_name === 'string' ? active.target_class_name : '',
      line: {
        x1: active.line?.x1_permille ?? 0,
        y1: active.line?.y1_permille ?? 0,
        x2: active.line?.x2_permille ?? 0,
        y2: active.line?.y2_permille ?? 0,
      },
      outside: {
        x: active.outside?.x_permille ?? 0,
        y: active.outside?.y_permille ?? 0,
      },
      conf_threshold_permille: active.conf_threshold_permille ?? 500,
      max_dist_permille: active.max_dist_permille ?? 100,
      track_history_k: active.track_history_k ?? 8,
      max_miss: active.max_miss ?? 5,
      k_confirm: active.k_confirm ?? 3,
      window_minutes: active.window_minutes ?? 30,
      tracks_report_enable: active.tracks_report_enable === true,
      heat_grid_enable: active.heat_grid_enable === true,
    }
    setDraft(candidate)
    setDraftBaseGeneration(
      typeof payload.config_generation === 'number' ? payload.config_generation : null,
    )
  }, [payload, draft])

  const applied = payload?.active_config || null
  const lineDraft: LineDraft | null = useMemo(() => {
    if (!draft) return null
    return {
      x1: draft.line.x1,
      y1: draft.line.y1,
      x2: draft.line.x2,
      y2: draft.line.y2,
      outX: draft.outside.x,
      outY: draft.outside.y,
    }
  }, [draft])

  const setLineDraft = (d: LineDraft) => {
    setDraft((prev) => (prev ? { ...prev, ...draftToLineFields(d) } : prev),)
  }

  const generationDrift =    draftBaseGeneration !== null
    && typeof payload?.config_generation === 'number'
    && payload.config_generation !== draftBaseGeneration

  const targetChanged =    draft !== null && applied !== null && draft.target_class_name !== applied.target_class_name

  const validation = draft
    ? validateLineConfigCandidate(draft)
    : { ok: false, reason: 'no_draft' }

  const sessionRunning = session?.state === 'running'
  const saveDisabled =    !sessionRunning
    || draft === null
    || !validation.ok
    || (targetChanged && !destructiveAck)

  const handleSave = async () => {
    if (!draft || !validation.ok) return
    await submitConfig({
      requestId: newRequestId(),
      expectedConfigGeneration: draftBaseGeneration ?? 0,
      payloadBase64: candidateToBase64(encodeLineConfigCandidate(draft)),
      destructiveConfirmed: targetChanged ? destructiveAck : false,
    })
  }

  const handleResetCounters = async () => {
    if (typeof payload?.config_generation !== 'number') return
    await submitReset({
      requestId: newRequestId(),
      expectedConfigGeneration: payload.config_generation,
      destructiveConfirmed: true,
    })
    setResetConfirmOpen(false)
  }

  const snapshotFresh =    snapshot !== null
    && snapshot.availability === 'fresh'
    && typeof snapshot.age_ms === 'number'
    && snapshot.age_ms <= SNAPSHOT_STALE_MS
  const snapshotStale =    snapshot !== null
    && (snapshot.availability === 'stale'
      || (typeof snapshot.age_ms === 'number' && snapshot.age_ms > SNAPSHOT_STALE_MS))

  const num = (v: unknown, fallback = '—') => (typeof v === 'number' ? String(v) : fallback)

  const videoStats = useMemo(() => {
    if (!payload) return null
    return {
      winIn: typeof payload.counters?.in === 'number' ? payload.counters.in : null,
      winOut: typeof payload.counters?.out === 'number' ? payload.counters.out : null,
      cumIn: typeof payload.cumulative?.in === 'number' ? payload.cumulative.in : null,
      cumOut: typeof payload.cumulative?.out === 'number' ? payload.cumulative.out : null,
    }
  }, [payload])

  const freshnessChip = (
    <span
      className={`shrink-0 rounded px-2 py-0.5 text-xs ${snapshotFresh ? 'bg-green-50 text-green-700' : snapshotStale ? 'bg-amber-50 text-amber-800' : 'bg-gray-100 text-gray-600'}`}
      data-testid="lc-freshness"
    >
      {snapshotFresh
        ? i18n._('sys.app_management.business.fresh')
        : snapshotStale
          ? i18n._('sys.app_management.business.stale')
          : i18n._('sys.app_management.business.snapshot_unavailable')}
    </span>
  )

  const renderLive = () => (
    <div className="space-y-3" data-testid="lc-live">
      {snapshot === null && (
        <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800" data-testid="lc-snapshot-missing">
          {i18n._('sys.app_management.business.snapshot_unavailable')}
        </div>
      )}
      {snapshotStale && (
        <div className="rounded border border-amber-300 bg-amber-50 p-2 text-xs text-amber-800" data-testid="lc-snapshot-stale">
          {i18n._('sys.app_management.business.snapshot_stale')}
        </div>
      )}
      {payload && (
        <div className="rounded-lg border border-[#e6eaee] p-3">
          <p className="mb-2 text-sm font-semibold text-[#273746]">
            {i18n._('sys.app_management.business.stats_title')}
          </p>
          <div className="grid grid-cols-1 gap-x-6 sm:grid-cols-2">
            <StatRow
              label={i18n._('sys.app_management.business.report_seq')}
              value={num(payload.report_seq)}
            />
            <StatRow
              label={i18n._('sys.app_management.business.app_state')}
              value={String(payload.app_state || 'unknown')}
            />
            <StatRow
              label={i18n._('sys.app_management.business.storage')}
              value={`${String(payload.storage?.state || 'unknown')}${payload.storage?.withheld === true ? ' · withheld' : ''}`}
              testId="lc-storage"
            />
            <StatRow
              label={i18n._('sys.app_management.business.model_valid')}
              value={payload.model?.valid === true ? i18n._('common.yes') : i18n._('common.no')}
            />
            <StatRow
              label={i18n._('sys.app_management.business.gap_gaps')}
              value={num(payload.gap?.gaps)}
            />
            <StatRow
              label={i18n._('sys.app_management.business.gap_lost')}
              value={`${num(payload.gap?.lost)} / ${num(payload.gap?.lost_unknown)}`}
            />
          </div>
          <div className="mt-3 border-t border-[#eef1f4] pt-2">
            {!resetConfirmOpen ? (
              <Button variant="outline" size="sm" onClick={() => setResetConfirmOpen(true)} data-testid="lc-reset-open">
                {i18n._('sys.app_management.business.reset_counters')}
              </Button>
            ) : (
              <div className="rounded border border-[#f0d8be] bg-[#fff8f1] p-2 text-sm" data-testid="lc-reset-confirm">
                <p className="text-[#745638]">{i18n._('sys.app_management.business.reset_confirm_text')}</p>
                <div className="mt-2 flex gap-2">
                  <Button variant="destructive" size="sm" onClick={handleResetCounters} data-testid="lc-reset-confirm-btn">
                    {i18n._('sys.app_management.business.reset_confirm_yes')}
                  </Button>
                  <Button variant="outline" size="sm" onClick={() => setResetConfirmOpen(false)} data-testid="lc-reset-cancel">
                    {i18n._('sys.app_management.uninstall.cancel')}
                  </Button>
                </div>
              </div>
            )}
          </div>
        </div>
      )}
      <div className="rounded-lg border border-[#e6eaee] p-3" data-testid="lc-reports">
        <p className="text-sm font-semibold text-[#273746]">
          {i18n._('sys.app_management.report.title')}
        </p>
        {reports === null && (
          <p className="mt-1 text-xs text-[#718093]">{i18n._('sys.app_management.report.unavailable')}</p>
        )}
        {reports !== null && reports.session_bound !== true && (
          <p className="mt-1 text-xs text-[#718093]" data-testid="lc-reports-unbound">
            {i18n._('sys.app_management.report.no_session')}
          </p>
        )}
        {reports !== null && reports.session_bound === true && (
          <div className="mt-2 space-y-1">
            {(reports.reports || []).length === 0 && (
              <p className="text-xs text-[#718093]">{i18n._('sys.app_management.report.empty')}</p>
            )}
            {(reports.reports || []).map((r, idx) => (
              <div
                key={`${r.report_seq}-${idx}`}
                className="flex flex-wrap items-center justify-between gap-2 rounded bg-[#f7f9fb] px-2 py-1.5 text-xs"
                data-testid={`lc-report-${idx}`}
              >
                <span className="font-mono text-[#334354]">#{r.report_seq}</span>
                <span className="text-[#334354]">
                  MQTT: {i18n._(reportStateKey(r.mqtt_state))} ({r.mqtt_attempts ?? 0})
                </span>
                <span className="text-[#334354]">
                  {i18n._('sys.app_management.report.webhook')}: {i18n._(reportStateKey(r.webhook_state))} ({r.webhook_attempts ?? 0})
                </span>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  )

  const renderConfig = () => (
    <div className="space-y-3" data-testid="lc-config">
      {draft === null && (
        <p className="text-sm text-[#718093]">{i18n._('sys.app_management.business.no_draft')}</p>
      )}
      {draft && (
        <>
          <div className="grid grid-cols-1 gap-2 sm:grid-cols-2">
            <div>
              <Label className="text-xs text-[#718093]">
                {i18n._('sys.app_management.business.counter_name')}
              </Label>
              <Input
                value={draft.counter_name}
                maxLength={63}
                onChange={(e) => setDraft((p) => (p ? { ...p, counter_name: (e.target as HTMLInputElement).value } : p))}
                data-testid="lc-counter-name"
              />
            </div>
            <div>
              <Label className="text-xs text-[#718093]">
                {i18n._('sys.app_management.business.target_class')}
              </Label>
              <Input
                value={draft.target_class_name}
                maxLength={31}
                onChange={(e) => setDraft((p) => (p ? { ...p, target_class_name: (e.target as HTMLInputElement).value } : p),)}
                data-testid="lc-target-class"
              />
            </div>
            <div>
              <Label className="text-xs text-[#718093]">
                {i18n._('sys.app_management.business.conf_threshold')}
              </Label>
              <Input
                type="number"
                min={0}
                max={1000}
                value={draft.conf_threshold_permille}
                onChange={(e) => setDraft((p) => (p
                      ? {
                          ...p,
                          conf_threshold_permille: Number((e.target as HTMLInputElement).value),
                        }
                      : p),)}
                data-testid="lc-conf-threshold"
              />
            </div>
          </div>
          <p className="text-xs text-[#718093]">
            {i18n._('sys.app_management.business.line_fields')}: x1={draft.line.x1} y1={draft.line.y1} x2=
            {draft.line.x2} y2={draft.line.y2} · out=({draft.outside.x},{draft.outside.y})
          </p>
          {draftBaseGeneration !== null && (
            <p className="font-mono text-xs text-[#718093]" data-testid="lc-draft-base">
              {i18n._('sys.app_management.business.draft_base')}: config_generation={draftBaseGeneration}
            </p>
          )}
        </>
      )}
    </div>
  )

  const renderAdvanced = () => (
    <div className="grid grid-cols-1 gap-2 sm:grid-cols-2" data-testid="lc-advanced">
      {draft === null ? (
        <p className="text-sm text-[#718093]">{i18n._('sys.app_management.business.no_draft')}</p>
      ) : (
        <>
          <div>
            <Label className="text-xs text-[#718093]">
              {i18n._('sys.app_management.business.max_dist')}
            </Label>
            <Input
              type="number"
              min={1}
              max={1000}
              value={draft.max_dist_permille}
              onChange={(e) => setDraft((p) => (p ? { ...p, max_dist_permille: Number((e.target as HTMLInputElement).value) } : p),)}
              data-testid="lc-max-dist"
            />
          </div>
          <div>
            <Label className="text-xs text-[#718093]">
              {i18n._('sys.app_management.business.track_history')}
            </Label>
            <Input
              type="number"
              min={4}
              max={16}
              value={draft.track_history_k}
              onChange={(e) => setDraft((p) => (p ? { ...p, track_history_k: Number((e.target as HTMLInputElement).value) } : p),)}
              data-testid="lc-track-history"
            />
          </div>
          <div>
            <Label className="text-xs text-[#718093]">
              {i18n._('sys.app_management.business.max_miss')}
            </Label>
            <Input
              type="number"
              min={1}
              value={draft.max_miss}
              onChange={(e) => setDraft((p) => (p ? { ...p, max_miss: Number((e.target as HTMLInputElement).value) } : p))}
              data-testid="lc-max-miss"
            />
          </div>
          <div>
            <Label className="text-xs text-[#718093]">
              {i18n._('sys.app_management.business.k_confirm')}
            </Label>
            <Input
              type="number"
              min={1}
              value={draft.k_confirm}
              onChange={(e) => setDraft((p) => (p ? { ...p, k_confirm: Number((e.target as HTMLInputElement).value) } : p),)}
              data-testid="lc-k-confirm"
            />
          </div>
          <div className="flex items-center justify-between rounded border border-[#e6eaee] px-3 py-2">
            <Label className="text-xs text-[#334354]">
              {i18n._('sys.app_management.business.tracks_report')}
            </Label>
            <Switch
              checked={draft.tracks_report_enable}
              onCheckedChange={(v) => setDraft((p) => (p ? { ...p, tracks_report_enable: v } : p))}
              data-testid="lc-tracks-toggle"
            />
          </div>
          <div className="flex items-center justify-between rounded border border-[#e6eaee] px-3 py-2">
            <Label className="text-xs text-[#334354]">
              {i18n._('sys.app_management.business.heat_grid')}
            </Label>
            <Switch
              checked={draft.heat_grid_enable}
              onCheckedChange={(v) => setDraft((p) => (p ? { ...p, heat_grid_enable: v } : p))}
              data-testid="lc-heat-toggle"
            />
          </div>
        </>
      )}
    </div>
  )

  return (
    <div className="flex h-full min-h-0 flex-col overflow-hidden p-4" data-testid="lc-page">
      {generationDrift && (
        <div className="mb-2 shrink-0 rounded border border-amber-300 bg-amber-50 p-2 text-xs text-amber-800" data-testid="lc-generation-drift">
          {i18n._('sys.app_management.business.generation_drift')}
        </div>
      )}
      <div className="grid min-h-0 flex-1 grid-cols-1 gap-3 lg:grid-cols-[minmax(0,3fr)_minmax(320px,1fr)]">
        <Card className="flex min-h-0 flex-col overflow-hidden">
          <CardContent className="flex min-h-0 flex-1 flex-col p-0">
            <VideoLineCanvas
              draft={lineDraft}
              onDraftChange={setLineDraft}
              invalidTarget={payload?.model?.valid === false}
              stats={videoStats}
            />
          </CardContent>
        </Card>
        <div className="flex min-h-0 flex-col gap-3">
          <div
            className="flex shrink-0 flex-wrap items-center justify-between gap-2 rounded-lg border border-[#e6eaee] bg-[#fbfcfe] p-3"
            data-testid="lc-stats-strip"
          >
            <div className="flex flex-wrap items-baseline">
              <StatBig
                testId="lc-cum-in"
                tone="green"
                value={payload ? num(payload.cumulative?.in) : '—'}
                label={i18n._('sys.app_management.business.cum_in')}
              />
              <StatBig
                testId="lc-cum-out"
                tone="red"
                value={payload ? num(payload.cumulative?.out) : '—'}
                label={i18n._('sys.app_management.business.cum_out')}
              />
              <StatBig
                testId="lc-win-in"
                tone="green"
                value={payload ? num(payload.counters?.in) : '—'}
                label={i18n._('sys.app_management.business.win_in')}
              />
              <StatBig
                testId="lc-win-out"
                tone="red"
                value={payload ? num(payload.counters?.out) : '—'}
                label={i18n._('sys.app_management.business.win_out')}
              />
            </div>
            {freshnessChip}
          </div>
          <div className="min-h-0 flex-1 overflow-y-auto pr-1">
            <div className="flex gap-4 border-b border-[#dfe4e9] pb-1 text-sm">
              {(
                [
                  ['live', 'sys.app_management.business.tab_live'],
                  ['config', 'sys.app_management.business.tab_config'],
                  ['advanced', 'sys.app_management.business.tab_advanced'],
                ] as Array<[InnerTab, string]>
              ).map(([key, label]) => (
                <button
                  key={key}
                  type="button"
                  onClick={() => setInnerTab(key)}
                  className={`pb-1 ${innerTab === key ? 'border-b-2 border-[#f45108] font-semibold text-[#f45108]' : 'text-[#8994a5]'}`}
                  data-testid={`lc-tab-${key}`}
                >
                  {i18n._(label)}
                </button>
              ))}
            </div>
            <div className="mt-3">
              {innerTab === 'live' && renderLive()}
              {innerTab === 'config' && renderConfig()}
              {innerTab === 'advanced' && renderAdvanced()}
            </div>
            <WatchBanner />
            {validation.ok === false && draft !== null && (
              <p className="mt-2 text-xs text-red-600" data-testid="lc-field-error">
                {i18n._('sys.app_management.business.invalid_field')}: {validation.reason}
              </p>
            )}
            {targetChanged && (
              <label className="mt-2 flex items-start gap-2 rounded border border-[#f0d8be] bg-[#fff8f1] p-2 text-xs text-[#745638]" data-testid="lc-destructive-ack">
                <input
                  type="checkbox"
                  checked={destructiveAck}
                  onChange={(e) => setDestructiveAck((e.target as HTMLInputElement).checked)}
                  data-testid="lc-destructive-checkbox"
                />
                <span>{i18n._('sys.app_management.business.destructive_ack')}</span>
              </label>
            )}
            <div className="mt-3 flex items-center justify-between gap-3 border-t border-[#dfe4e9] pt-3" data-testid="lc-save-bar">
              <div className="flex items-center gap-2 text-sm text-[#3a4857]">
                <span className="font-medium">{i18n._('sys.app_management.business.stats_report_title')}</span>
                <span className="text-xs text-[#8994a5]">
                  {i18n._('sys.app_management.business.window_minutes')}
                </span>
                <Input
                  type="number"
                  min={1}
                  max={1440}
                  className="w-20"
                  value={draft?.window_minutes ?? ''}
                  onChange={(e) => setDraft((p) => (p ? { ...p, window_minutes: Number((e.target as HTMLInputElement).value) } : p),)}
                  data-testid="lc-window-minutes"
                />
              </div>
              <Button
                size="sm"
                className="text-[#f45108]"
                variant="outline"
                onClick={handleSave}
                disabled={saveDisabled}
                data-testid="lc-save"
              >
                {i18n._('sys.app_management.business.save')}
              </Button>
            </div>
            {payload && payload.schema !== LC_SNAPSHOT_SCHEMA && (
              <p className="mt-2 font-mono text-xs text-amber-700" data-testid="lc-schema-warn">
                {i18n._('sys.app_management.business.schema_mismatch')}
              </p>
            )}
            <p className="mt-2 pb-1 text-xs text-[#8290a0]">
              {i18n._('sys.app_management.business.identity_note')}: {record.publisherId} / {record.appId} {record.version}
            </p>
          </div>
        </div>
      </div>
    </div>
  )
}
