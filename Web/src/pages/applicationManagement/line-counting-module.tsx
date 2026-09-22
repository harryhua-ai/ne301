import { useState, useEffect, useCallback, useMemo, useRef } from 'preact/hooks';
import { toast } from 'sonner';
import { useLingui } from '@lingui/react';
import { RefreshCw } from 'lucide-react';
import { Card, CardContent } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import lineCounting, {
    type LineCountingConfig,
    type LineCountingStatus,
    type LineCountingStats,
    type LineCountingEvents,
} from '@/services/api/line-counting';
import LineToolbar from './lineCounting/LineToolbar';
import ConfigPage from './lineCounting/ConfigPage';
import AdvancedPage from './lineCounting/AdvancedPage';
import StatsAndEvents from './lineCounting/StatsAndEvents';
import VideoPreview, { type LineTrackDot } from './lineCounting/VideoPreview';

type EditPhase = 0 | 1 | 2;
type LcPage = 'realtime' | 'config' | 'advanced';

const PAGES: Array<{ key: LcPage; labelKey: string }> = [
    { key: 'realtime', labelKey: 'sys.line_counting.page_realtime' },
    { key: 'config', labelKey: 'sys.line_counting.page_config' },
    { key: 'advanced', labelKey: 'sys.line_counting.page_advanced' },
];

const clampPm = (v: number) => Math.max(0, Math.min(1000, Math.round(v)));

function computeOutside(x1: number, y1: number, x2: number, y2: number, sign: number) {
    const mx = (x1 + x2) / 2;
    const my = (y1 + y2) / 2;
    let nx = -(y2 - y1);
    let ny = x2 - x1;
    const nlen = Math.hypot(nx, ny) || 1;
    nx /= nlen;
    ny /= nlen;
    const OFF = 150;
    return {
        ox: clampPm(mx + sign * nx * OFF),
        oy: clampPm(my + sign * ny * OFF),
    };
}

export default function LineCountingModule() {
    const { i18n } = useLingui();
    const [config, setConfig] = useState<LineCountingConfig | null>(null);
    const [draft, setDraft] = useState<LineCountingConfig | null>(null);
    const [status, setStatus] = useState<LineCountingStatus | null>(null);
    const [stats, setStats] = useState<LineCountingStats | null>(null);
    const [events, setEvents] = useState<LineCountingEvents | null>(null);
    const [saving, setSaving] = useState(false);
    const [confirmReset, setConfirmReset] = useState(false);
    const [confirmTarget, setConfirmTarget] = useState(false);
    const [page, setPage] = useState<LcPage>('realtime');

    const [editMode, setEditMode] = useState(false);
    const [editPhase, setEditPhase] = useState<EditPhase>(0);
    const configRef = useRef<LineCountingConfig | null>(null);
    configRef.current = config;

    const applyLoadedConfig = useCallback((cfg: LineCountingConfig) => {
        setConfig(cfg);
        setDraft(cfg);
    }, []);

    const loadConfig = useCallback(async () => {
        const res = await lineCounting.getConfig();
        applyLoadedConfig(res.data as LineCountingConfig);
    }, [applyLoadedConfig]);

    const pollRuntime = useCallback(async () => {
        if (!configRef.current) {
            const res = await lineCounting.getConfig().catch(() => null);
            if (res) {
                const cfg = res.data as LineCountingConfig;
                setConfig(cfg);
                setDraft((prev) => prev ?? cfg);
            }
        }
        try {
            const [s, st, ev] = await Promise.all([
                lineCounting.getStatus(),
                lineCounting.getStats(),
                lineCounting.getEvents(),
            ]);
            setStatus(s.data as LineCountingStatus);
            setStats(st.data as LineCountingStats);
            setEvents(ev.data as LineCountingEvents);
        } catch (e) {
            console.error('Failed to poll line counting runtime', e);
        }
    }, []);

    useEffect(() => {
        pollRuntime();
        const interval = setInterval(pollRuntime, 2000);
        return () => clearInterval(interval);
    }, [pollRuntime]);

    const dirty = useMemo(() => JSON.stringify(draft) !== JSON.stringify(config), [draft, config]);

    const handleDraftChange = (patch: Partial<LineCountingConfig>) => {
        setDraft((prev) => (prev ? { ...prev, ...patch } : prev));
    };

    const handleDraftLine = (x1: number, y1: number, x2: number, y2: number, ox: number, oy: number,) => {
        setDraft((prev) => (prev ? {
            ...prev,
            line: { ...prev.line, x1, y1, x2, y2, outside_x: ox, outside_y: oy },
        } : prev));
    };

    const handlePickPoint = useCallback((x: number, y: number) => {
        const d = draft;
        if (!d) return;
        if (editPhase === 0) {
            handleDraftLine(x, y, x, y, d.line.outside_x, d.line.outside_y);
            setEditPhase(1);
        } else if (editPhase === 1) {
            const { ox, oy } = computeOutside(d.line.x1, d.line.y1, x, y, 1);
            handleDraftLine(d.line.x1, d.line.y1, x, y, ox, oy);
            setEditPhase(2);
        } else {
            handleDraftLine(x, y, x, y, d.line.outside_x, d.line.outside_y);
            setEditPhase(1);
        }
    }, [draft, editPhase]);

    const handleFlipDirection = () => {
        const d = draft;
        if (!d) return;
        if (d.line.x1 === d.line.x2 && d.line.y1 === d.line.y2) return;
        const mx = (d.line.x1 + d.line.x2) / 2;
        const my = (d.line.y1 + d.line.y2) / 2;
        handleDraftLine(
            d.line.x1,
            d.line.y1,
            d.line.x2,
            d.line.y2,
            clampPm(2 * mx - d.line.outside_x),
            clampPm(2 * my - d.line.outside_y),
        );
    };

    const draftHasLine = !!draft && (draft.line.x1 !== draft.line.x2 || draft.line.y1 !== draft.line.y2);

    const targetChanged = !!draft && !!config && draft.target_class !== config.target_class;

    const doSave = async () => {
        if (!draft) return;
        setSaving(true);
        try {
            await lineCounting.setConfig(draft);
            loadConfig().catch(() => null);
            setEditMode(false);
            setEditPhase(0);
            toast.success(i18n._('sys.line_counting.save_success'));
        } catch (e: unknown) {
            const net = (e as { code?: string })?.code === 'ERR_NETWORK';
            const serverMsg = (e as { data?: { message?: string } })?.data?.message;
            const base = net ? i18n._('sys.line_counting.save_failed_net') : i18n._('sys.line_counting.save_failed');
            toast.error(serverMsg ? `${base}: ${serverMsg}` : base);
        } finally {
            setSaving(false);
        }
    };

    const handleSave = async () => {
        if (!draft) return;
        if (targetChanged) {
            setConfirmTarget(true);
            return;
        }
        await doSave();
    };

    const draftLineValid = !!draft && (draft.line.x1 !== draft.line.x2 || draft.line.y1 !== draft.line.y2);

    const handleFinishDraw = async () => {
        if (!draft) return;
        if (!dirty || !draftLineValid) {
            setEditMode(false);
            setEditPhase(0);
            return;
        }
        setEditMode(false);
        setEditPhase(0);
        await handleSave();
    };

    const [resetBusy, setResetBusy] = useState(false);

    const waitResetIdle = async (tries: number): Promise<void> => {
        if (tries <= 0) return;
        const busy = await lineCounting.isResetting();
        if (!busy) return;
        await new Promise((resolve) => {
            setTimeout(resolve, 800);
        });
        await waitResetIdle(tries - 1);
    };

    const handleReset = async () => {
        setResetBusy(true);
        try {
            await lineCounting.reset();
            await waitResetIdle(30);
            await pollRuntime();
            toast.success(i18n._('sys.line_counting.reset_success'));
        } catch {
            toast.error(i18n._('sys.line_counting.reset_failed_net'));
        }
        setResetBusy(false);
        setConfirmReset(false);
    };

    const tracks: LineTrackDot[] = [];

    return (
      <div className="flex flex-col gap-3 p-4">
            <Card className="overflow-hidden">
                <CardContent className="p-0">
                    <VideoPreview
                      config={config}
                      draft={draft}
                      editMode={editMode}
                      editPhase={editPhase}
                      state={status?.state}
                      tracks={tracks}
                      onPickPoint={handlePickPoint}
                    />
                </CardContent>
            </Card>

            <LineToolbar
              editMode={editMode}
              editPhase={editPhase}
              hasLine={draftHasLine}
              onToggleEdit={() => {
                    setEditMode(!editMode);
                    setEditPhase(0);
                }}
              onFinishDraw={handleFinishDraw}
              onResetLine={() => {
                    setDraft((prev) => (prev ? { ...prev, line: { x1: 500, y1: 500, x2: 500, y2: 500, outside_x: 500, outside_y: 500 } } : prev));
                    setEditMode(false);
                    setEditPhase(0);
                }}
              onFlipDirection={handleFlipDirection}
            />

            <div className="grid grid-cols-3 border-b border-gray-200" role="tablist" data-testid="lc-page-tabs">
                {PAGES.map((p) => (
                    <button
                      key={p.key}
                      role="tab"
                      aria-selected={page === p.key}
                      data-testid={`lc-tab-${p.key}`}
                      onClick={() => setPage(p.key)}
                      className={`px-2 py-2 text-sm -mb-px border-b-2 text-center transition-colors ${page === p.key ? 'border-primary text-primary font-medium' : 'border-transparent text-gray-500 hover:text-gray-800'}`}
                    >
                        {i18n._(p.labelKey)}
                    </button>
                ))}
            </div>

            {page === 'realtime' && (
                <div className="space-y-3" data-testid="lc-page-realtime">
                    <StatsAndEvents
                      stats={stats}
                      events={events}
                      targetClass={status?.target_class ?? ''}
                    />
                    <div className="flex justify-end">
                        <Button variant="outline" onClick={() => setConfirmReset(true)}>
                            {i18n._('sys.line_counting.reset_stats')}
                        </Button>
                    </div>
                </div>
            )}

            {page === 'config' && config && draft && (
                <ConfigPage
                  config={draft}
                  status={status}
                  onChange={handleDraftChange}
                />
            )}

            {page === 'advanced' && config && draft && (
                <AdvancedPage
                  config={draft}
                  onChange={handleDraftChange}
                />
            )}

            {page !== 'realtime' && config && draft && (
                <div className="flex justify-end pt-1" data-testid="lc-save-bar">
                    <Button
                      onClick={handleSave}
                      disabled={saving || !draftLineValid}
                      title={draftLineValid ? undefined : i18n._('sys.line_counting.line_required')}
                    >
                        {saving ? i18n._('sys.line_counting.saving') : i18n._('common.save')}
                    </Button>
                </div>
            )}

            {confirmTarget && (
                <div className="fixed inset-0 z-50 bg-black/40 flex items-center justify-center" data-testid="lc-confirm-dialog">
                    <div className="bg-white rounded-lg p-5 w-80 space-y-3">
                        <h4 className="font-semibold text-sm">{i18n._('sys.line_counting.confirm_target_title')}</h4>
                        <p className="text-xs text-gray-500">{i18n._('sys.line_counting.confirm_target_body')}</p>
                        <div className="flex gap-2 justify-end">
                            <Button size="sm" variant="outline" onClick={() => setConfirmTarget(false)}>{i18n._('common.cancel')}</Button>
                            <Button
                              size="sm"
                              onClick={async () => {
                                    setConfirmTarget(false);
                                    await doSave();
                                }}
                            >
                                {i18n._('common.confirm')}
                            </Button>
                        </div>
                    </div>
                </div>
            )}

            {resetBusy && (
                <div className="fixed inset-0 z-[60] bg-black/50 flex items-center justify-center" data-testid="lc-reset-busy">
                    <div className="bg-white rounded-lg p-6 flex flex-col items-center gap-3">
                        <RefreshCw className="w-6 h-6 animate-spin text-primary" />
                        <span className="text-sm text-gray-700">{i18n._('sys.line_counting.reset_busy')}</span>
                    </div>
                </div>
            )}
            {confirmReset && (
                <div className="fixed inset-0 z-50 bg-black/40 flex items-center justify-center" data-testid="lc-reset-dialog">
                    <div className="bg-white rounded-lg p-5 w-80 space-y-3">
                        <h4 className="font-semibold text-sm">{i18n._('sys.line_counting.reset_stats_title')}</h4>
                        <p className="text-xs text-gray-500">{i18n._('sys.line_counting.reset_stats_body')}</p>
                        <div className="flex gap-2 justify-end">
                            <Button size="sm" variant="outline" onClick={() => setConfirmReset(false)}>{i18n._('common.cancel')}</Button>
                            <Button size="sm" onClick={handleReset}>{i18n._('sys.line_counting.confirm_reset')}</Button>
                        </div>
                    </div>
                </div>
            )}
            {dirty && <span className="hidden" data-testid="lc-dirty" />}
      </div>
    );
}
