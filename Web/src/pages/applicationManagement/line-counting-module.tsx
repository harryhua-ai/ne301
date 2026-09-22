import { useState, useEffect, useCallback, useMemo } from 'preact/hooks';
import { toast } from 'sonner';
import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import { Button } from '@/components/ui/button';
import lineCounting, {
    type LineCountingConfig,
    type LineCountingStatus,
    type LineCountingStats,
    type LineCountingEvents,
} from '@/services/api/line-counting';
import ConfigPanel from './lineCounting/ConfigPanel';
import StatsAndEvents from './lineCounting/StatsAndEvents';
import VideoPreview, { type LineTrackDot } from './lineCounting/VideoPreview';

type EditPhase = 0 | 1 | 2;

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

    const [editMode, setEditMode] = useState(false);
    const [editPhase, setEditPhase] = useState<EditPhase>(0);

    const loadConfig = useCallback(async () => {
        const res = await lineCounting.getConfig();
        const cfg = res.data as LineCountingConfig;
        setConfig(cfg);
        setDraft(cfg);
    }, []);

    const pollRuntime = useCallback(async () => {
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
        const init = async () => {
            try {
                await loadConfig();
            } catch (e) {
                console.error('Failed to load config', e);
            }
            await pollRuntime();
        };
        init();
        const interval = setInterval(pollRuntime, 2000);
        return () => clearInterval(interval);
    }, [loadConfig, pollRuntime]);

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

    const targetChanged = !!draft && !!config && draft.target_class !== config.target_class;

    const doSave = async () => {
        if (!draft) return;
        setSaving(true);
        try {
            await lineCounting.setConfig(draft);
            await loadConfig();
            setEditMode(false);
            setEditPhase(0);
            toast.success(i18n._('sys.line_counting.save_success'));
        } catch (e: unknown) {
            const msg =                (e as { response?: { data?: { message?: string } } })?.response?.data?.message
                || i18n._('sys.line_counting.save_failed');
            toast.error(String(msg));
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

    const handleReset = async () => {
        try {
            await lineCounting.reset();
            await pollRuntime();
            toast.success(i18n._('sys.line_counting.reset_success'));
        } catch {
            toast.error(i18n._('sys.line_counting.reset_failed'));
        }
        setConfirmReset(false);
    };

    const tracks: LineTrackDot[] = [];

    return (
      <div className="flex flex-col h-full p-4 gap-3 overflow-hidden">
            <div className="flex-1 grid grid-cols-1 lg:grid-cols-[minmax(0,1fr)_280px] gap-3 min-h-0 items-start">
                <div className="min-h-0 flex flex-col gap-3">
                    <Card className="flex flex-col overflow-hidden">
                        <CardContent className="flex flex-col p-0">
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
                    <StatsAndEvents stats={stats} events={events} />
                </div>
                <div className="min-h-0 overflow-y-auto pr-1">
                    {config && draft ? (
                        <ConfigPanel
                          config={draft}
                          status={status}
                          saving={saving}
                          editMode={editMode}
                          editPhase={editPhase}
                          hasLine={!!draft && (draft.line.x1 !== draft.line.x2 || draft.line.y1 !== draft.line.y2)}
                          onChange={handleDraftChange}
                          onSave={handleSave}
                          onToggleEdit={() => {
                                setEditMode(!editMode);
                                setEditPhase(0);
                            }}
                          onResetLine={() => {
                                handleDraftLine(500, 500, 500, 500, 500, 500);
                                setEditPhase(0);
                            }}
                          onFlipDirection={handleFlipDirection}
                          onReset={() => setConfirmReset(true)}
                        />
                    ) : (
                        <div className="flex items-center justify-center h-32">
                            <span className="text-gray-400">{i18n._('sys.line_counting.loading')}</span>
                        </div>
                    )}
                </div>
            </div>

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
