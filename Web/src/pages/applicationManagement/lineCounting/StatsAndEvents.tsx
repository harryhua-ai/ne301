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
        <div className="flex flex-col items-center min-w-0" data-testid="lc-stat-cell">
            <span className={`max-w-full truncate text-[28px] leading-[34px] font-semibold font-mono tabular-nums ${accent ?? 'text-gray-800'}`}>{value}</span>
            <span className="max-w-full truncate mt-1 text-[13px] leading-none text-gray-500">{label}</span>
        </div>
    );
}

const COLS = 'grid grid-cols-[64px_minmax(64px,1.3fr)_64px_minmax(56px,1fr)] lg:grid-cols-[80px_150px_80px_minmax(0,1fr)] items-center gap-x-2 px-1';

export default function StatsAndEvents({ stats, events, targetClass }: StatsAndEventsProps) {
    const { i18n } = useLingui();
    const list = events?.events ?? [];

    return (
        <div className="flex flex-col lg:flex-row gap-3">
            <Card className="w-full lg:w-[38%] lg:shrink-0 h-72 py-0 gap-0">
                <CardContent className="p-5 flex flex-col flex-1 min-h-0">
                    <h4 className="text-base font-semibold leading-6 text-gray-800">{i18n._('sys.line_counting.stats_title')}</h4>
                    <div className="flex-1 min-h-0 flex items-center justify-center">
                        <div className="w-[240px] max-w-full grid grid-cols-[repeat(2,minmax(0,1fr))] gap-x-4">
                            <StatCell label={i18n._('sys.line_counting.stat_window_in')} value={stats?.window.in ?? 0} accent="text-emerald-600" />
                            <StatCell label={i18n._('sys.line_counting.stat_window_out')} value={stats?.window.out ?? 0} accent="text-rose-600" />
                            <div className="col-span-2 my-3 h-px bg-gray-100" />
                            <StatCell label={i18n._('sys.line_counting.stat_total_in')} value={stats?.total.in ?? 0} accent="text-emerald-600" />
                            <StatCell label={i18n._('sys.line_counting.stat_total_out')} value={stats?.total.out ?? 0} accent="text-rose-600" />
                        </div>
                    </div>
                </CardContent>
            </Card>
            <Card className="w-full lg:flex-1 lg:min-w-0 h-72 py-0 gap-0">
                <CardContent className="p-5 flex flex-col flex-1 min-h-0">
                    <h4 className="text-base font-semibold leading-6 text-gray-800">{i18n._('sys.line_counting.events_title')}</h4>
                    <div className={`${COLS} h-7 mt-4 text-[10px] md:text-[11px] text-gray-400 border-b border-gray-100`}>
                        <span>{i18n._('sys.line_counting.col_track')}</span>
                        <span>{i18n._('sys.line_counting.col_object')}</span>
                        <span>{i18n._('sys.line_counting.col_direction')}</span>
                        <span>{i18n._('sys.line_counting.col_time')}</span>
                    </div>
                    <div className="flex-1 min-h-0 overflow-y-auto pr-1" data-testid="lc-event-list">
                        {list.length === 0 ? (
                            <div className="flex items-center justify-center h-full text-xs text-gray-400">{i18n._('sys.line_counting.no_events')}</div>
                        ) : (
                            <ul>
                                {list.map((e) => {
                                    const eventClass = (e as { target_class?: string }).target_class || targetClass || '—';
                                    return (
                                        <li key={e.sequence} className={`${COLS} h-[30px] text-xs font-mono border-b border-gray-50`}>
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
