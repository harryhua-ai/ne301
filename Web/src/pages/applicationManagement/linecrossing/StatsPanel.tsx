import { useState } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import {
  Dialog,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
  DialogFooter,
} from '@/components/dialog';
import {
  SNAPSHOT_STALE_MS,
  type BusinessSnapshotResponse,
  type ReportStatusResponse,
} from '@/services/api/appSession';
import type { LineCrossingStatsView } from './lineCrossingAdapter';

export interface StatsPanelProps {
  stats: LineCrossingStatsView | null;
  snapshot: BusinessSnapshotResponse | null;
  sessionState: string;
  reports: ReportStatusResponse | null;
  resetDisabled: boolean;
  onReset: () => Promise<void>;
}

const REPORT_STATE_KEYS: Record<string, string> = {
  not_configured: 'sys.app_management.report.state.not_configured',
  pending: 'sys.app_management.report.state.pending',
  succeeded: 'sys.app_management.report.state.succeeded',
  failed: 'sys.app_management.report.state.failed',
  unknown: 'sys.app_management.report.state.unknown',
};

function reportStateKey(state: string | undefined): string {
  return (
    REPORT_STATE_KEYS[String(state || '').toLowerCase()]
    || REPORT_STATE_KEYS.unknown
  );
}

function StatBig({
  value,
  label,
  tone,
  testId,
}: {
  value: string | number;
  label: string;
  tone: 'green' | 'red';
  testId: string;
}) {
  const color = tone === 'green' ? 'text-green-600' : 'text-red-600';
  return (
    <div className="flex items-baseline gap-1.5 px-2 py-0.5">
      <div
        className={`text-xl font-bold leading-none ${color}`}
        data-testid={testId}
      >
        {value}
      </div>
      <div className="text-[10px] leading-tight text-gray-500">{label}</div>
    </div>
  );
}

export default function StatsPanel({
  stats,
  snapshot,
  sessionState,
  reports,
  resetDisabled,
  onReset,
}: StatsPanelProps) {
  const { i18n } = useLingui();
  const [resetOpen, setResetOpen] = useState(false);
  const [resetting, setResetting] = useState(false);
  const availability = String(snapshot?.availability || 'unavailable');
  const fresh =    availability === 'fresh'
    && typeof snapshot?.age_ms === 'number'
    && snapshot.age_ms <= SNAPSHOT_STALE_MS;
  const stale =    availability === 'stale'
    || (typeof snapshot?.age_ms === 'number'
      && snapshot.age_ms > SNAPSHOT_STALE_MS);
  const stateKey =    sessionState === 'running'
      ? 'sys.app_management.session.state_running'
      : sessionState === 'stopping'
        ? 'sys.app_management.session.state_stopping'
        : 'sys.app_management.session.state_idle';

  const handleReset = async () => {
    setResetting(true);
    try {
      await onReset();
    } finally {
      setResetting(false);
      setResetOpen(false);
    }
  };

  return (
    <>
      <Card className="shrink-0" data-testid="lc-stats-strip">
        <CardContent className="flex flex-wrap items-center gap-3 px-3 py-1.5">
          <div className="flex items-center gap-1 divide-x divide-gray-200 dark:divide-gray-700">
            <StatBig
              tone="green"
              value={stats?.window_in ?? '—'}
              label={`${i18n._('sys.pc.in')} (${i18n._('sys.pc.current_window')})`}
              testId="lc-win-in"
            />
            <StatBig
              tone="red"
              value={stats?.window_out ?? '—'}
              label={`${i18n._('sys.pc.out')} (${i18n._('sys.pc.current_window')})`}
              testId="lc-win-out"
            />
            <StatBig
              tone="green"
              value={stats?.total_in ?? '—'}
              label={i18n._('sys.pc.total_in')}
              testId="lc-cum-in"
            />
            <StatBig
              tone="red"
              value={stats?.total_out ?? '—'}
              label={i18n._('sys.pc.total_out')}
              testId="lc-cum-out"
            />
          </div>
          <span
            className={`shrink-0 rounded px-2 py-0.5 text-xs ${fresh ? 'bg-green-50 text-green-700' : stale ? 'bg-amber-50 text-amber-800' : 'bg-gray-100 text-gray-600'}`}
            data-testid="lc-freshness"
          >
            {fresh
              ? i18n._('sys.app_management.business.fresh')
              : stale
                ? i18n._('sys.app_management.business.stale')
                : i18n._('sys.app_management.business.snapshot_unavailable')}
          </span>
          <span
            className="text-xs text-gray-600"
            data-testid="lc-session-state"
          >
            {i18n._('sys.app_management.session.title')}: {i18n._(stateKey)}
          </span>
          <div className="flex-1" />
          <Button
            variant="destructive"
            size="sm"
            className="h-7"
            onClick={() => setResetOpen(true)}
            disabled={resetDisabled}
            data-testid="lc-reset-open"
          >
            {i18n._('sys.pc.reset_totals')}
          </Button>
          <div
            className="w-full text-[10px] font-mono leading-tight text-gray-500"
            data-testid="lc-stats-details"
          >
            <span>
              {i18n._('sys.app_management.business.report_seq')}:{' '}
              {stats?.report_seq ?? '—'}
            </span>
            <span className="ml-3">
              {i18n._('sys.app_management.business.gap_gaps')}:{' '}
              {stats?.gap_gaps ?? '—'}
            </span>
            <span className="ml-3">
              {i18n._('sys.app_management.business.gap_lost')}:{' '}
              {stats?.gap_lost ?? '—'} / {stats?.gap_lost_unknown ?? '—'}
            </span>
          </div>
        </CardContent>
      </Card>
      <div
        className="rounded-lg border border-[#e6eaee] p-3"
        data-testid="lc-reports"
      >
        <p className="text-sm font-semibold text-[#273746]">
          {i18n._('sys.app_management.report.title')}
        </p>
        {reports === null && (
          <p className="mt-1 text-xs text-[#718093]">
            {i18n._('sys.app_management.report.unavailable')}
          </p>
        )}
        {reports !== null && reports.session_bound !== true && (
          <p
            className="mt-1 text-xs text-[#718093]"
            data-testid="lc-reports-unbound"
          >
            {i18n._('sys.app_management.report.no_session')}
          </p>
        )}
        {reports !== null && reports.session_bound === true && (
          <div className="mt-2 space-y-1">
            {(reports.reports || []).length === 0 && (
              <p className="text-xs text-[#718093]">
                {i18n._('sys.app_management.report.empty')}
              </p>
            )}
            {(reports.reports || []).map((report, index) => (
              <div
                key={`${report.report_seq}-${index}`}
                className="flex flex-wrap items-center justify-between gap-2 rounded bg-[#f7f9fb] px-2 py-1.5 text-xs"
                data-testid={`lc-report-${index}`}
              >
                <span className="font-mono text-[#334354]">
                  #{report.report_seq}
                </span>
                <span className="text-[#334354]">
                  MQTT: {i18n._(reportStateKey(report.mqtt_state))} (
                  {report.mqtt_attempts ?? 0})
                </span>
                <span className="text-[#334354]">
                  {i18n._('sys.app_management.report.webhook')}:{' '}
                  {i18n._(reportStateKey(report.webhook_state))} (
                  {report.webhook_attempts ?? 0})
                </span>
              </div>
            ))}
          </div>
        )}
      </div>
      <Dialog open={resetOpen} onOpenChange={setResetOpen}>
        <DialogContent>
          <div data-testid="lc-reset-confirm">
            <DialogHeader>
              <DialogTitle>{i18n._('sys.pc.reset_confirm_title')}</DialogTitle>
              <DialogDescription>
                {i18n._('sys.app_management.business.reset_confirm_text')}
              </DialogDescription>
            </DialogHeader>
            <DialogFooter>
              <Button variant="outline" onClick={() => setResetOpen(false)}>
                {i18n._('sys.pc.cancel')}
              </Button>
              <Button
                variant="destructive"
                onClick={handleReset}
                disabled={resetting}
                data-testid="lc-reset-confirm-btn"
              >
                {resetting
                  ? i18n._('sys.pc.resetting')
                  : i18n._('sys.pc.confirm')}
              </Button>
            </DialogFooter>
          </div>
        </DialogContent>
      </Dialog>
    </>
  );
}
