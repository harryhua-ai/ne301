import { useCallback, useEffect, useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import type { AppRecord } from '@/store/appManagement';
import { useAppSessionStore } from '@/store/appSession';
import {
  candidateToBase64,
  encodeLineConfigCandidate,
  isV3LineCrossingCapable,
  newRequestId,
  SNAPSHOT_STALE_MS,
  validateLineConfigCandidate,
} from '@/services/api/appSession';
import type { LineConfigCandidateInput } from '@/services/api/appSession';
import ConfigPanel from './ConfigPanel';
import StatsPanel from './StatsPanel';
import VideoPreview from './VideoPreview';
import {
  candidateFromEditorConfig,
  configFromSnapshot,
  statsFromSnapshot,
  type LineCrossingEditorConfig,
} from './lineCrossingAdapter';

type EditPhase = 0 | 1 | 2;

const clampPermille = (value: number) => Math.max(0, Math.min(1000, Math.round(value)));

function computeOutside(
  x1: number,
  y1: number,
  x2: number,
  y2: number,
  sign: number
) {
  const midpointX = (x1 + x2) / 2;
  const midpointY = (y1 + y2) / 2;
  let normalX = -(y2 - y1);
  let normalY = x2 - x1;
  const normalLength = Math.hypot(normalX, normalY) || 1;
  normalX /= normalLength;
  normalY /= normalLength;
  const offset = 150;
  return {
    outsideX: clampPermille(midpointX + sign * normalX * offset),
    outsideY: clampPermille(midpointY + sign * normalY * offset),
  };
}

function WatchBanner() {
  const { i18n } = useLingui();
  const watch = useAppSessionStore(state => state.watch);
  const snapshot = useAppSessionStore(state => state.snapshot);
  const resetWatch = useAppSessionStore(state => state.resetWatch);

  if (watch.phase === 'idle') return null;
  if (watch.phase === 'submitting') {
    return (
      <div
        className="mt-2 rounded border border-[#e6eaee] bg-[#f7f9fb] p-2 text-sm text-[#334354]"
        data-testid="lc-watch-submitting"
      >
        {i18n._('sys.app_management.receipt.submitting')}
      </div>
    );
  }
  if (watch.phase === 'watching') {
    return (
      <div
        className="mt-2 rounded border border-[#e6eaee] bg-[#f7f9fb] p-2 text-sm text-[#334354]"
        data-testid="lc-watch-pending"
      >
        {i18n._('sys.app_management.receipt.waiting_receipt')}
      </div>
    );
  }
  if (watch.phase === 'refused') {
    return (
      <div
        className="mt-2 rounded border border-red-300 bg-red-50 p-2 text-sm text-red-700"
        data-testid="lc-watch-refused"
      >
        <p>{i18n._('sys.app_management.receipt.refused')}</p>
        {watch.reason && (
          <p className="mt-1 font-mono text-xs">{watch.reason}</p>
        )}
        <Button
          variant="outline"
          size="sm"
          className="mt-2"
          onClick={resetWatch}
          data-testid="lc-watch-dismiss"
        >
          {i18n._('sys.app_management.receipt.dismiss')}
        </Button>
      </div>
    );
  }
  const state = watch.cmdState;
  if (state === 'applied' && !watch.timedOut) {
    return (
      <div
        className="mt-2 rounded border border-green-300 bg-green-50 p-2 text-sm text-green-800"
        data-testid="lc-watch-applied"
      >
        {i18n._('sys.app_management.receipt.applied')}
        {watch.resultConfigGeneration !== null && (
          <span className="ml-1 font-mono text-xs">
            config_generation={watch.resultConfigGeneration}
          </span>
        )}
      </div>
    );
  }
  if (watch.timedOut || state === 'unknown') {
    return (
      <div
        className="mt-2 rounded border border-amber-300 bg-amber-50 p-2 text-sm text-amber-800"
        data-testid="lc-watch-unknown"
      >
        <p>{i18n._('sys.app_management.receipt.unknown')}</p>
        <p className="mt-1 text-xs">
          {i18n._('sys.app_management.receipt.unknown_hint')}
          {typeof snapshot?.config_generation === 'number' && (
            <span className="ml-1 font-mono">
              (config_generation={snapshot.config_generation})
            </span>
          )}
        </p>
        <Button
          variant="outline"
          size="sm"
          className="mt-2"
          onClick={resetWatch}
          data-testid="lc-watch-dismiss"
        >
          {i18n._('sys.app_management.receipt.dismiss')}
        </Button>
      </div>
    );
  }
  return (
    <div
      className="mt-2 rounded border border-red-300 bg-red-50 p-2 text-sm text-red-700"
      data-testid="lc-watch-terminal"
    >
      <p>{i18n._(`sys.app_management.receipt.state.${state}`)}</p>
      {watch.resultConfigGeneration !== null && (
        <p className="mt-1 font-mono text-xs">
          config_generation={watch.resultConfigGeneration}
        </p>
      )}
      <Button
        variant="outline"
        size="sm"
        className="mt-2"
        onClick={resetWatch}
        data-testid="lc-watch-dismiss"
      >
        {i18n._('sys.app_management.receipt.dismiss')}
      </Button>
    </div>
  );
}

export default function LineCrossingPage({ record }: { record: AppRecord }) {
  const { i18n } = useLingui();
  const session = useAppSessionStore(state => state.session);
  const snapshot = useAppSessionStore(state => state.snapshot);
  const payload = useAppSessionStore(state => state.snapshotPayload);
  const reports = useAppSessionStore(state => state.reports);
  const watch = useAppSessionStore(state => state.watch);
  const refreshSnapshotAndReports = useAppSessionStore(
    state => state.refreshSnapshotAndReports
  );
  const submitConfig = useAppSessionStore(state => state.submitConfig);
  const submitReset = useAppSessionStore(state => state.submitReset);
  const [config, setConfig] = useState<LineCrossingEditorConfig | null>(null);
  const [draftBaseGeneration, setDraftBaseGeneration] = useState<number | null>(
    null
  );
  const [editMode, setEditMode] = useState(false);
  const [editPhase, setEditPhase] = useState<EditPhase>(0);
  const [destructiveAck, setDestructiveAck] = useState(false);

  useEffect(() => {
    refreshSnapshotAndReports();
    const timer = window.setInterval(() => refreshSnapshotAndReports(), 5000);
    return () => window.clearInterval(timer);
  }, [refreshSnapshotAndReports]);

  useEffect(() => {
    if (config !== null) return;
    const nextConfig = configFromSnapshot(payload);
    if (!nextConfig) return;
    setConfig(nextConfig);
    setDraftBaseGeneration(
      typeof payload?.config_generation === 'number'
        ? payload.config_generation
        : null
    );
  }, [config, payload]);

  useEffect(() => {
    if (
      watch.phase !== 'terminal'
      || watch.msgType !== 'config'
      || watch.cmdState !== 'applied'
    ) return;
    if (
      typeof watch.resultConfigGeneration !== 'number'
      || payload?.config_generation !== watch.resultConfigGeneration
    ) return;
    const applied = configFromSnapshot(payload);
    if (!applied) return;
    setConfig(applied);
    setDraftBaseGeneration(watch.resultConfigGeneration);
    setDestructiveAck(false);
    setEditMode(false);
    setEditPhase(0);
  }, [payload, watch]);

  const handleConfigChange = (patch: Partial<LineCrossingEditorConfig>) => {
    setConfig(previous => (previous ? { ...previous, ...patch } : previous));
  };

  const handleLineChange = (
    x1: number,
    y1: number,
    x2: number,
    y2: number,
    outsideX: number,
    outsideY: number
  ) => {
    setConfig(previous => (previous
        ? {
            ...previous,
            line_x1_permille: x1,
            line_y1_permille: y1,
            line_x2_permille: x2,
            line_y2_permille: y2,
            outside_x_permille: outsideX,
            outside_y_permille: outsideY,
          }
        : previous));
  };

  const handlePickPoint = useCallback(
    (x: number, y: number) => {
      if (!config) return;
      if (editPhase === 0) {
        handleLineChange(
          x,
          y,
          x,
          y,
          config.outside_x_permille,
          config.outside_y_permille
        );
        setEditPhase(1);
      } else if (editPhase === 1) {
        const outside = computeOutside(
          config.line_x1_permille,
          config.line_y1_permille,
          x,
          y,
          1
        );
        handleLineChange(
          config.line_x1_permille,
          config.line_y1_permille,
          x,
          y,
          outside.outsideX,
          outside.outsideY
        );
        setEditPhase(2);
      } else {
        handleLineChange(
          x,
          y,
          x,
          y,
          config.outside_x_permille,
          config.outside_y_permille
        );
        setEditPhase(1);
      }
    },
    [config, editPhase]
  );

  const handleFlipDirection = () => {
    if (!config) return;
    if (
      config.line_x1_permille === config.line_x2_permille
      && config.line_y1_permille === config.line_y2_permille
    ) return;
    const midpointX = (config.line_x1_permille + config.line_x2_permille) / 2;
    const midpointY = (config.line_y1_permille + config.line_y2_permille) / 2;
    handleLineChange(
      config.line_x1_permille,
      config.line_y1_permille,
      config.line_x2_permille,
      config.line_y2_permille,
      clampPermille(2 * midpointX - config.outside_x_permille),
      clampPermille(2 * midpointY - config.outside_y_permille)
    );
  };

  const hasLine = Boolean(
    config
      && (config.line_x1_permille !== config.line_x2_permille
        || config.line_y1_permille !== config.line_y2_permille)
  );
  const appliedConfig = configFromSnapshot(payload);
  const targetChanged = Boolean(
    config
      && appliedConfig
      && config.target_class_name !== appliedConfig.target_class_name
  );
  const generationDrift =    draftBaseGeneration !== null
    && typeof payload?.config_generation === 'number'
    && payload.config_generation !== draftBaseGeneration;
  const candidate: LineConfigCandidateInput | null = config
    ? candidateFromEditorConfig(config)
    : null;
  const validation = candidate
    ? validateLineConfigCandidate(candidate)
    : { ok: false, reason: 'no_draft' };
  const capable = isV3LineCrossingCapable(session, snapshot);
  const saveDisabled =    !capable
    || !config
    || !validation.ok
    || (targetChanged && !destructiveAck)
    || watch.phase === 'submitting'
    || watch.phase === 'watching';
  const stats = statsFromSnapshot(payload);

  const handleToggleEdit = () => {
    setEditMode(current => !current);
    setEditPhase(0);
  };

  const handleResetLine = () => {
    setEditPhase(0);
    handleLineChange(500, 500, 500, 500, 500, 500);
  };

  const handleSave = async () => {
    if (!candidate || !validation.ok) return;
    await submitConfig({
      requestId: newRequestId(),
      expectedConfigGeneration: draftBaseGeneration ?? 0,
      payloadBase64: candidateToBase64(encodeLineConfigCandidate(candidate)),
      destructiveConfirmed: targetChanged ? destructiveAck : false,
    });
  };

  const handleResetCounters = async () => {
    if (typeof payload?.config_generation !== 'number') return;
    await submitReset({
      requestId: newRequestId(),
      expectedConfigGeneration: payload.config_generation,
      destructiveConfirmed: true,
    });
  };

  const snapshotMissing = snapshot === null || payload === null;
  const snapshotStale =    snapshot !== null
    && (snapshot.availability === 'stale'
      || (typeof snapshot.age_ms === 'number'
        && snapshot.age_ms > SNAPSHOT_STALE_MS));

  return (
    <div
      className="flex h-full min-h-0 flex-col overflow-hidden p-4"
      data-testid="lc-page"
    >
      <div className="grid min-h-0 flex-1 grid-cols-1 gap-3 lg:grid-cols-[minmax(0,3fr)_minmax(320px,1fr)]">
        <Card className="flex min-h-0 flex-col overflow-hidden">
          <CardContent className="flex min-h-0 flex-1 flex-col p-0">
            <VideoPreview
              config={config}
              stats={stats}
              editMode={editMode}
              editPhase={editPhase}
              onPickPoint={handlePickPoint}
            />
          </CardContent>
        </Card>
        <div className="flex min-h-0 flex-col gap-3">
          <StatsPanel
            stats={stats}
            snapshot={snapshot}
            sessionState={String(session?.state || 'idle')}
            reports={reports}
            resetDisabled={
              !capable || typeof payload?.config_generation !== 'number'
            }
            onReset={handleResetCounters}
          />
          {snapshotMissing && (
            <div
              className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
              data-testid="lc-snapshot-missing"
            >
              {i18n._('sys.app_management.business.snapshot_unavailable')}
            </div>
          )}
          {snapshotStale && (
            <div
              className="rounded border border-amber-300 bg-amber-50 p-2 text-xs text-amber-800"
              data-testid="lc-snapshot-stale"
            >
              {i18n._('sys.app_management.business.snapshot_stale')}
            </div>
          )}
          <div className="min-h-0 flex-1 overflow-y-auto pr-1">
            {config === null ? (
              <div className="flex h-32 items-center justify-center">
                <span className="text-gray-400">
                  {i18n._('sys.app_management.business.no_draft')}
                </span>
              </div>
            ) : (
              <>
                <ConfigPanel
                  config={config}
                  modelName={
                    session?.model?.model_name
                    || payload?.model?.target_class
                    || ''
                  }
                  onChange={handleConfigChange}
                  onSave={handleSave}
                  saving={
                    watch.phase === 'submitting' || watch.phase === 'watching'
                  }
                  saveDisabled={saveDisabled}
                  editMode={editMode}
                  editPhase={editPhase}
                  hasLine={hasLine}
                  generationDrift={generationDrift}
                  targetChanged={targetChanged}
                  destructiveAck={destructiveAck}
                  onDestructiveAckChange={setDestructiveAck}
                  validationReason={
                    validation.ok ? null : validation.reason || 'invalid'
                  }
                  onToggleEdit={handleToggleEdit}
                  onResetLine={handleResetLine}
                  onFlipDirection={handleFlipDirection}
                />
                <WatchBanner />
                <p className="pb-1 pt-2 text-xs text-[#8290a0]">
                  {i18n._('sys.app_management.business.identity_note')}:{' '}
                  {record.publisherId} / {record.appId} {record.version}
                </p>
              </>
            )}
          </div>
        </div>
      </div>
    </div>
  );
}
