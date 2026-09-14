import { useState, useEffect, useCallback } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import { toast } from 'sonner';
import { Card, CardContent } from '@/components/ui/card';
import peopleCounting, { type PeopleCountingConfig, type PeopleCountingStats } from '@/services/api/peopleCounting';
import ConfigPanel from './ConfigPanel';
import StatsPanel from './StatsPanel';
import VideoPreview from './VideoPreview';

type EditPhase = 0 | 1 | 2;   // 0: pick L1, 1: pick L2, 2: line done (flip/save)

const clampPm = (v: number) => Math.max(0, Math.min(1000, Math.round(v)));

/** Auto-compute the "outside" reference point: perpendicular to the line, one side. */
function computeOutside(x1: number, y1: number, x2: number, y2: number, sign: number) {
    const mx = (x1 + x2) / 2; const 
my = (y1 + y2) / 2;
    let nx = -(y2 - y1); let 
ny = (x2 - x1);
    const nlen = Math.hypot(nx, ny) || 1; nx /= nlen; ny /= nlen;
    const OFF = 150;   // 15% of frame
    return { ox: clampPm(mx + sign * nx * OFF), oy: clampPm(my + sign * ny * OFF) };
}

export default function PeopleCountingPage() {
    const { i18n } = useLingui();
    const [config, setConfig] = useState<PeopleCountingConfig | null>(null);
    const [stats, setStats] = useState<PeopleCountingStats | null>(null);
    const [isLoading, setIsLoading] = useState(true);
    const [saving, setSaving] = useState(false);

    const [editMode, setEditMode] = useState(false);
    const [editPhase, setEditPhase] = useState<EditPhase>(0);

    const loadConfig = useCallback(async () => {
        try {
            const res = await peopleCounting.getConfig();
            setConfig(res.data as PeopleCountingConfig);
        } catch (e) {
            console.error('Failed to load config', e);
        }
    }, []);

    const loadStats = useCallback(async () => {
        try {
            const res = await peopleCounting.getStats();
            setStats(res.data as PeopleCountingStats);
        } catch (e) {
            console.error('Failed to load stats', e);
        }
    }, []);

    useEffect(() => {
        const init = async () => {
            setIsLoading(true);
            await loadConfig();
            await loadStats();
            setIsLoading(false);
        };
        init();
    }, [loadConfig, loadStats]);

    useEffect(() => {
        const interval = setInterval(() => { loadStats(); }, 5000);
        return () => clearInterval(interval);
    }, [loadStats]);

    const handleConfigChange = (patch: Partial<PeopleCountingConfig>) => {
        setConfig((prev) => (prev ? { ...prev, ...patch } : prev));
    };

    const handleLineChange = (x1: number, y1: number, x2: number, y2: number, ox: number, oy: number) => {
        setConfig((prev) => (prev ? {
            ...prev,
            line_x1_permille: x1,
line_y1_permille: y1,
            line_x2_permille: x2,
line_y2_permille: y2,
            outside_x_permille: ox,
outside_y_permille: oy,
        } : prev));
    };

    // Two-click line: click 1 = L1, click 2 = L2 (outside auto-derived). A 3rd click restarts.
    const handlePickPoint = useCallback((x: number, y: number) => {
        const c = config;
        if (!c) return;
        if (editPhase === 0) {
            handleLineChange(x, y, x, y, c.outside_x_permille, c.outside_y_permille);
            setEditPhase(1);
        } else if (editPhase === 1) {
            const { ox, oy } = computeOutside(c.line_x1_permille, c.line_y1_permille, x, y, 1);
            handleLineChange(c.line_x1_permille, c.line_y1_permille, x, y, ox, oy);
            setEditPhase(2);
        } else {
            // restart from a fresh L1
            handleLineChange(x, y, x, y, c.outside_x_permille, c.outside_y_permille);
            setEditPhase(1);
        }
    }, [config, editPhase]);

    // Flip IN/OUT by mirroring the outside point through the line midpoint.
    const handleFlipDirection = () => {
        const c = config;
        if (!c) return;
        if (c.line_x1_permille === c.line_x2_permille && c.line_y1_permille === c.line_y2_permille) return;
        const mx = (c.line_x1_permille + c.line_x2_permille) / 2;
        const my = (c.line_y1_permille + c.line_y2_permille) / 2;
        handleLineChange(
            c.line_x1_permille, 
c.line_y1_permille, 
c.line_x2_permille, 
c.line_y2_permille,
clampPm(2 * mx - c.outside_x_permille), 
clampPm(2 * my - c.outside_y_permille),
        );
    };

    const hasLine = !!config && (
        config.line_x1_permille !== config.line_x2_permille
        || config.line_y1_permille !== config.line_y2_permille
    );

    const handleToggleEdit = () => {
        if (editMode) {
            setEditMode(false);
            setEditPhase(0);
        } else {
            setEditMode(true);
            setEditPhase(0);
        }
    };

    const handleResetLine = () => {
        setEditPhase(0);
        handleLineChange(500, 500, 500, 500, 500, 500);
    };

    const handleSave = async () => {
        if (!config) return;
        setSaving(true);
        try {
            await peopleCounting.setConfig(config);
            await loadConfig();
            setEditMode(false);
            setEditPhase(0);
            toast.success(i18n._('sys.pc.save_success'));
        } catch (e: unknown) {
            console.error('Save failed', e);
            // Surface the server's validation message (e.g. degenerate line);
            // skipErrorToast is set on this request so no global toast fired.
            const msg =
                (e as { response?: { data?: { message?: string } } })?.response?.data?.message
                || i18n._('sys.pc.save_failed');
            toast.error(String(msg));
        } finally {
            setSaving(false);
        }
    };

    return (
        <div className="flex flex-col h-full p-4 overflow-hidden">
            <div className="flex-1 grid grid-cols-1 lg:grid-cols-[minmax(0,3fr)_minmax(320px,1fr)] gap-3 min-h-0">
                <Card className="flex flex-col min-h-0 overflow-hidden">
                    <CardContent className="flex-1 min-h-0 flex flex-col p-0">
                        <VideoPreview
                          config={config}
                          stats={stats}
                          editMode={editMode}
                          editPhase={editPhase}
                          onPickPoint={handlePickPoint}
                        />
                    </CardContent>
                </Card>

                <div className="flex flex-col gap-3 min-h-0">
                    <div className="shrink-0">
                        <StatsPanel stats={stats} onResetDone={loadStats} />
                    </div>
                    <div className="flex-1 min-h-0 overflow-y-auto pr-1">
                        {isLoading || !config ? (
                            <div className="flex items-center justify-center h-32">
                                <span className="text-gray-400">Loading...</span>
                            </div>
                        ) : (
                            <ConfigPanel
                              config={config}
                              onChange={handleConfigChange}
                              onSave={handleSave}
                              saving={saving}
                              editMode={editMode}
                              editPhase={editPhase}
                              hasLine={hasLine}
                              onToggleEdit={handleToggleEdit}
                              onResetLine={handleResetLine}
                              onFlipDirection={handleFlipDirection}
                            />
                        )}
                    </div>
                </div>
            </div>
        </div>
    );
}
