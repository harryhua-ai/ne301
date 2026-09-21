import { Card, CardContent } from '@/components/ui/card';
import type { LineCountingStats, LineCountingEvents } from '@/services/api/line-counting';

export interface StatsAndEventsProps {
    stats: LineCountingStats | null;
    events: LineCountingEvents | null;
}

function StatCell({ label, value, accent }: { label: string; value: number; accent?: string }) {
    return (
        <div className="flex flex-col items-center justify-center py-3" data-testid="lc-stat-cell">
            <span className={`text-2xl font-semibold font-mono ${accent ?? 'text-gray-800'}`}>{value}</span>
            <span className="text-xs text-gray-500 mt-1">{label}</span>
        </div>
    );
}

export default function StatsAndEvents({ stats, events }: StatsAndEventsProps) {
    const list = events?.events ?? [];
    return (
        <div className="grid grid-cols-2 gap-3">
            <Card>
                <CardContent className="p-3">
                    <h4 className="text-sm font-semibold text-gray-800 mb-1">实时统计</h4>
                    <div className="grid grid-cols-4 divide-x">
                        <StatCell label="本周期进入" value={stats?.window.in ?? 0} accent="text-emerald-600" />
                        <StatCell label="本周期离开" value={stats?.window.out ?? 0} accent="text-rose-600" />
                        <StatCell label="累计进入" value={stats?.total.in ?? 0} accent="text-emerald-600" />
                        <StatCell label="累计离开" value={stats?.total.out ?? 0} accent="text-rose-600" />
                    </div>
                </CardContent>
            </Card>
            <Card>
                <CardContent className="p-3">
                    <h4 className="text-sm font-semibold text-gray-800 mb-1">最近过线事件</h4>
                    <div className="h-24 overflow-y-auto pr-1" data-testid="lc-event-list">
                        {list.length === 0 ? (
                            <div className="flex items-center justify-center h-full text-xs text-gray-400">暂无事件</div>
                        ) : (
                            <ul className="space-y-1">
                                {list.map((e) => (
                                    <li key={e.sequence} className="flex items-center justify-between text-xs font-mono">
                                        <span className="text-gray-500">#{e.track_id}</span>
                                        <span className={e.direction === 'in' ? 'text-emerald-600 font-semibold' : 'text-rose-600 font-semibold'}>
                                            {e.direction === 'in' ? 'IN' : 'OUT'}
                                        </span>
                                        <span className="text-gray-400">{Math.round(e.timestamp_ms / 1000)}s 前</span>
                                    </li>
                                ))}
                            </ul>
                        )}
                    </div>
                </CardContent>
            </Card>
        </div>
    );
}
