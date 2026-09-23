import { useLingui } from '@lingui/react';
import { Card, CardContent } from '@/components/ui/card';
import type { LineCountingStats, LineCountingEvents } from '@/services/api/line-counting';

export interface StatsAndEventsProps {
    stats: LineCountingStats | null;
    events: LineCountingEvents | null;
    targetClass: string;
}

function StatCell({ label, value, accent }: { label: string; value: number; accent?: string }) {
    return (
        <div className="flex flex-col items-start py-2 min-w-0 px-1" data-testid="lc-stat-cell">
            <span className={`max-w-full truncate text-lg sm:text-xl md:text-2xl font-semibold font-mono tabular-nums ${accent ?? 'text-gray-800'}`}>{value}</span>
            <span className="max-w-full truncate text-[10px] md:text-xs text-gray-500 mt-0.5 leading-tight">{label}</span>
        </div>
    );
}

const COLS = 'grid grid-cols-[72px_minmax(0,1fr)_64px_88px] items-center gap-x-2 px-1';

export default function StatsAndEvents({ stats, events, targetClass }: StatsAndEventsProps) {
    const { i18n } = useLingui();
    const list = events?.events ?? [];

    return (
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-3 items-stretch">
            <Card className="flex flex-col">
                <CardContent className="p-3 flex-1 flex flex-col">
                    <h4 className="text-sm font-semibold text-gray-800 mb-1">{i18n._('sys.line_counting.stats_title')}</h4>
                    <div className="grid grid-cols-2 gap-x-2 gap-y-1 flex-1 content-center">
                        <StatCell label={i18n._('sys.line_counting.stat_window_in')} value={stats?.window.in ?? 0} accent="text-emerald-600" />
                        <StatCell label={i18n._('sys.line_counting.stat_window_out')} value={stats?.window.out ?? 0} accent="text-rose-600" />
                        <StatCell label={i18n._('sys.line_counting.stat_total_in')} value={stats?.total.in ?? 0} accent="text-emerald-600" />
                        <StatCell label={i18n._('sys.line_counting.stat_total_out')} value={stats?.total.out ?? 0} accent="text-rose-600" />
                    </div>
                </CardContent>
            </Card>
            <Card className="flex flex-col">
                <CardContent className="p-3 flex-1 flex flex-col">
                    <h4 className="text-sm font-semibold text-gray-800 mb-1">{i18n._('sys.line_counting.events_title')}</h4>
                    <div className={`${COLS} text-[10px] md:text-[11px] text-gray-400 pb-1 border-b border-gray-100`}>
                        <span>{i18n._('sys.line_counting.col_track')}</span>
                        <span>{i18n._('sys.line_counting.col_object')}</span>
                        <span>{i18n._('sys.line_counting.col_direction')}</span>
                        <span>{i18n._('sys.line_counting.col_time')}</span>
                    </div>
                    <div className="flex-1 min-h-28 max-h-72 overflow-y-auto pr-1" data-testid="lc-event-list">
                        {list.length === 0 ? (
                            <div className="flex items-center justify-center h-full text-xs text-gray-400">{i18n._('sys.line_counting.no_events')}</div>
                        ) : (
                            <ul>
                                {list.map((e) => {
                                    const eventClass = (e as { target_class?: string }).target_class || targetClass || '—';
                                    return (
                                        <li key={e.sequence} className={`${COLS} text-xs font-mono py-1 border-b border-gray-50`}>
                                            <span className="text-gray-500">#{e.track_id}</span>
                                            <span className="truncate text-gray-700" data-testid="lc-event-class">{eventClass}</span>
                                            <span className={e.direction === 'in' ? 'text-emerald-600 font-semibold' : 'text-rose-600 font-semibold'}>
                                                {e.direction === 'in' ? 'IN' : 'OUT'}
                                            </span>
                                            <span className="text-gray-400 truncate">{i18n._('sys.line_counting.seconds_ago').replace('{n}', String(Math.round(e.timestamp_ms / 1000)))}</span>
                                        </li>
                                    );
                                })}
                            </ul>
                        )}
                    </div>
                </CardContent>
            </Card>
        </div>
    );
}
