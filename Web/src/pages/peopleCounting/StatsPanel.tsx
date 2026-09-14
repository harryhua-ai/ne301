import { useState, useEffect, useCallback } from 'preact/hooks';
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
import peopleCounting, { type PeopleCountingStats, type BacklogInfo } from '@/services/api/peopleCounting';

export interface StatsPanelProps {
    stats: PeopleCountingStats | null;
    onResetDone: () => void;
}

function StatBig({
    value,
    label,
    tone,
}: {
    value: string | number;
    label: string;
    tone: 'green' | 'red';
}) {
    const color = tone === 'green' ? 'text-green-600' : 'text-red-600';
    return (
        <div className="flex items-baseline gap-1.5 px-2 py-0.5">
            <div className={`text-xl font-bold leading-none ${color}`}>{value}</div>
            <div className="text-[10px] text-gray-500 leading-tight">{label}</div>
        </div>
    );
}

export default function StatsPanel({ stats, onResetDone }: StatsPanelProps) {
    const { i18n } = useLingui();
    const [backlog, setBacklog] = useState<BacklogInfo | null>(null);
    const [resetOpen, setResetOpen] = useState(false);
    const [resetting, setResetting] = useState(false);

    const loadBacklog = useCallback(async () => {
        try {
            const res = await peopleCounting.getBacklog();
            setBacklog(res.data as BacklogInfo);
        } catch (e) {
            console.error('Failed to load backlog', e);
        }
    }, []);

    useEffect(() => {
        loadBacklog();
    }, [loadBacklog]);

    const handleReset = async () => {
        setResetting(true);
        try {
            await peopleCounting.resetTotals();
            await loadBacklog();
            onResetDone();
        } catch (e) {
            console.error('Reset failed', e);
        } finally {
            setResetting(false);
            setResetOpen(false);
        }
    };

    // Firmware sends osKernelGetTickCount() — a monotonic MILLISECOND tick,
    // not a Unix epoch (kind: "monotonic"). Render it as uptime; switch to a
    // wall-clock date only if the device ever reports kind "rtc".
    const fmtTs = (ts: number, kind?: string) => {
        if (!ts) return '--';
        if (kind === 'rtc') return new Date(ts).toLocaleString();
        const s = Math.floor(ts / 1000);
        const h = Math.floor(s / 3600);
        const m = Math.floor((s % 3600) / 60);
        const sec = s % 60;
        if (h > 0) return `${h}h ${m}m`;
        if (m > 0) return `${m}m ${sec}s`;
        return `${sec}s`;
    };

    return (
        <>
            <Card className="shrink-0">
                <CardContent className="py-1.5 px-3 flex items-center gap-3 flex-wrap">
                    {/* Big stats — tight inline */}
                    <div className="flex items-center gap-1 divide-x divide-gray-200 dark:divide-gray-700">
                        <StatBig
                          tone="green"
                          value={stats?.window_in ?? '--'}
                          label={`${i18n._('sys.pc.in')} (${i18n._('sys.pc.current_window')})`}
                        />
                        <StatBig
                          tone="red"
                          value={stats?.window_out ?? '--'}
                          label={`${i18n._('sys.pc.out')} (${i18n._('sys.pc.current_window')})`}
                        />
                        <StatBig
                          tone="green"
                          value={stats?.total_in ?? '--'}
                          label={i18n._('sys.pc.total_in')}
                        />
                        <StatBig
                          tone="red"
                          value={stats?.total_out ?? '--'}
                          label={i18n._('sys.pc.total_out')}
                        />
                    </div>

                    {/* Compact details */}
                    <div className="flex flex-col gap-0.5 text-[10px] text-gray-500 font-mono leading-tight">
                        <span>{i18n._('sys.pc.last_report')}: <span className="text-gray-700 dark:text-gray-300">{stats ? fmtTs(stats.last_report_ts, stats.boot_id_kind) : '--'}</span></span>
                        <span>drop mq/wh: <span className="text-gray-700 dark:text-gray-300">{stats?.dropped_windows_mqtt ?? 0}/{stats?.dropped_windows_webhook ?? 0}</span> · backlog: <span className="text-gray-700 dark:text-gray-300">{backlog?.mqtt.count ?? 0}/{backlog?.webhook.count ?? 0}</span></span>
                        <span>dbg: sub <span className="text-gray-700 dark:text-gray-300">{stats?.dbg_sub_calls ?? '--'}</span> · type <span className="text-gray-700 dark:text-gray-300">{stats?.dbg_last_type ?? '--'}</span>(1=OD) · det <span className="text-gray-700 dark:text-gray-300">{stats?.dbg_last_nb_detect ?? '--'}</span> · matched <span className="text-blue-600 dark:text-blue-400">{stats?.dbg_matched_person ?? '--'}</span> · cls &quot;<span className="text-gray-700 dark:text-gray-300">{stats?.dbg_last_class ?? '--'}</span>&quot; · pos <span className="text-emerald-600 dark:text-emerald-400">{stats?.dbg_last_det_x ?? '--'},{stats?.dbg_last_det_y ?? '--'}</span></span>
                    </div>

                    <div className="flex-1" />

                    <Button
                      variant="destructive"
                      size="sm"
                      className="h-7"
                      onClick={() => setResetOpen(true)}
                    >
                        {i18n._('sys.pc.reset_totals')}
                    </Button>
                </CardContent>
            </Card>

            <Dialog open={resetOpen} onOpenChange={setResetOpen}>
                <DialogContent>
                    <DialogHeader>
                        <DialogTitle>{i18n._('sys.pc.reset_confirm_title')}</DialogTitle>
                        <DialogDescription>{i18n._('sys.pc.reset_confirm_desc')}</DialogDescription>
                    </DialogHeader>
                    <DialogFooter>
                        <Button variant="outline" onClick={() => setResetOpen(false)}>
                            {i18n._('sys.pc.cancel')}
                        </Button>
                        <Button variant="destructive" onClick={handleReset} disabled={resetting}>
                            {resetting ? i18n._('sys.pc.resetting') : i18n._('sys.pc.confirm')}
                        </Button>
                    </DialogFooter>
                </DialogContent>
            </Dialog>
        </>
    );
}
