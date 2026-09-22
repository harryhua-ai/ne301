import { useEffect, useRef, useState, useCallback } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import H264Player from '@/lib/MSE/h264Player';
import deviceTool from '@/services/api/deviceTool';
import { getWebSocketUrl } from '@/utils';
import { Button } from '@/components/ui/button';
import { RefreshCw } from 'lucide-react';
import type { LineCountingConfig, LineCountingState } from '@/services/api/line-counting';

export interface LineTrackDot {
    track_id: number;
    points: Array<{ x: number; y: number }>;
}

export interface VideoPreviewProps {
    config: LineCountingConfig | null;
    draft: LineCountingConfig | null;
    editMode: boolean;
    editPhase: 0 | 1 | 2;
    state?: LineCountingState;
    tracks?: LineTrackDot[];
    onPickPoint: (permilleX: number, permilleY: number) => void;
}

const STATE_BADGE_KEY: Record<string, string> = {
    running: 'sys.line_counting.state_running',
    disabled: 'sys.line_counting.state_disabled',
    unsupported_model: 'sys.line_counting.state_unsupported_model',
    target_class_invalid: 'sys.line_counting.state_target_invalid',
};

export default function VideoPreview({
    config,
    draft,
    editMode,
    editPhase,
    state,
    tracks = [],
    onPickPoint,
}: VideoPreviewProps) {
    const { i18n } = useLingui();
    const playerRef = useRef<H264Player | null>(null);
    const videoRef = useRef<HTMLVideoElement>(null);
    const stageRef = useRef<HTMLDivElement>(null);
    const videoUrlRef = useRef<string>('');
    const restartCountRef = useRef(0);
    const [reloading, setReloading] = useState(false);
    const [size, setSize] = useState({ w: 0, h: 0 });
    const [aspect, setAspect] = useState(16 / 9);
    const [connState] = useState<'ok' | 'reconnecting'>('ok');

    useEffect(() => {
        const videoUrl = getWebSocketUrl('/stream', 8081);
        videoUrlRef.current = videoUrl;
        const init = async () => {
            try {
                await deviceTool.startVideoStreamReq();
                const video = videoRef.current;
                if (!video) return;
                playerRef.current = new H264Player((msg) => {
                    if (msg.t === 'mseError') {
                        const count = ++restartCountRef.current;
                        if (count <= 3) {
                            setTimeout(() => { playerRef.current?.hardRestart(); }, 500);
                        }
                    }
                });
                playerRef.current.initPlayer(video);
                playerRef.current.start(videoUrl);
            } catch (e) {
                console.error('Failed to start video stream', e);
            }
        };
        init();
        return () => {
            playerRef.current?.destroy();
            playerRef.current = null;
            deviceTool.stopVideoStreamReq().catch(() => {});
        };
    }, []);

    useEffect(() => {
        const el = stageRef.current;
        if (!el) return;
        const update = () => setSize({ w: el.clientWidth, h: el.clientHeight });
        update();
        const ro = new ResizeObserver(update);
        ro.observe(el);
        return () => ro.disconnect();
    }, []);

    const handleStageClick = useCallback((e: MouseEvent) => {
        if (!editMode) return;
        const el = stageRef.current;
        if (!el) return;
        const rect = el.getBoundingClientRect();
        if (rect.width === 0 || rect.height === 0) return;
        const x = Math.max(0, Math.min(1000, Math.round(((e.clientX - rect.left) / rect.width) * 1000)));
        const y = Math.max(0, Math.min(1000, Math.round(((e.clientY - rect.top) / rect.height) * 1000)));
        onPickPoint(x, y);
    }, [editMode, onPickPoint]);

    const { w, h } = size;
    const p2px = (permille: number, dim: number) => (permille / 1000) * dim;
    const lineDirty = !!config && !!draft && JSON.stringify(draft.line) !== JSON.stringify(config.line);
    const primary = editMode || lineDirty || !config ? draft ?? config : config;
    const hasLine = !!primary && (
        primary.line.x1 !== primary.line.x2 || primary.line.y1 !== primary.line.y2
    );
    const draftHasLine = !!draft && (
        draft.line.x1 !== draft.line.x2 || draft.line.y1 !== draft.line.y2
    );
    const showActiveLine = lineDirty && !!config && (
        config.line.x1 !== config.line.x2 || config.line.y1 !== config.line.y2
    ) && draftHasLine;
    const showOverlay = (config?.enable || editMode || lineDirty) && w > 0 && h > 0;
    const x1 = primary ? p2px(primary.line.x1, w) : 0;
    const y1 = primary ? p2px(primary.line.y1, h) : 0;
    const x2 = primary ? p2px(primary.line.x2, w) : 0;
    const y2 = primary ? p2px(primary.line.y2, h) : 0;
    const ox = primary ? p2px(primary.line.outside_x, w) : 0;
    const oy = primary ? p2px(primary.line.outside_y, h) : 0;
    const ax1 = config ? p2px(config.line.x1, w) : 0;
    const ay1 = config ? p2px(config.line.y1, h) : 0;
    const ax2 = config ? p2px(config.line.x2, w) : 0;
    const ay2 = config ? p2px(config.line.y2, h) : 0;

    const mx = (x1 + x2) / 2;
    const my = (y1 + y2) / 2;
    let dx = mx - ox;
    let dy = my - oy;
    const dlen = Math.hypot(dx, dy) || 1;
    dx /= dlen;
    dy /= dlen;
    const aLen = Math.max(30, Math.min(w, h) * 0.07);
    const tipX = mx + dx * aLen;
    const tipY = my + dy * aLen;
    const baseX = mx + dx * aLen * 0.3;
    const baseY = my + dy * aLen * 0.3;
    const ang = Math.atan2(dy, dx);
    const head = 13;
    const h1x = tipX - head * Math.cos(ang - Math.PI / 6);
    const h1y = tipY - head * Math.sin(ang - Math.PI / 6);
    const h2x = tipX - head * Math.cos(ang + Math.PI / 6);
    const h2y = tipY - head * Math.sin(ang + Math.PI / 6);

    const badgeText = state ? i18n._(STATE_BADGE_KEY[state]) : null;

    const hint = !editMode ? null
        : editPhase === 0 ? i18n._('sys.line_counting.hint_start')
        : editPhase === 1 ? i18n._('sys.line_counting.hint_end')
        : i18n._('sys.line_counting.hint_draft');

    return (
        <div className="relative w-full h-full flex items-center justify-center min-h-0 bg-black">
            <div
              ref={stageRef}
              className={`relative w-full ${editMode ? 'cursor-crosshair' : ''}`}
              onClick={handleStageClick}
              data-testid="lc-stage"
            >
                <video
                  ref={videoRef}
                  className="block w-full h-auto"
                  id="lcVideoPlayer"
                  style={{ aspectRatio: String(aspect) }}
                  muted
                  playsInline
                  disableRemotePlayback
                  onLoadedMetadata={(e) => {
                        const v = e.currentTarget;
                        if (v.videoWidth && v.videoHeight) setAspect(v.videoWidth / v.videoHeight);
                    }}
                />

                {showOverlay && (
                    <svg className="absolute inset-0 w-full h-full pointer-events-none" viewBox={`0 0 ${w} ${h}`} data-testid="lc-overlay">
                        {tracks.map((t) => (
                            <g key={t.track_id} data-testid="lc-track">
                                {t.points.length > 1 && (
                                    <polyline
                                      points={t.points.map((p) => `${p2px(p.x, w)},${p2px(p.y, h)}`).join(' ')}
                                      fill="none"
                                      stroke="rgba(56,189,248,0.75)"
                                      strokeWidth={2}
                                    />
                                )}
                                {t.points.length > 0 && (
                                    <circle
                                      cx={p2px(t.points[t.points.length - 1].x, w)}
                                      cy={p2px(t.points[t.points.length - 1].y, h)}
                                      r={5}
                                      fill="rgba(56,189,248,0.9)"
                                    />
                                )}
                            </g>
                        ))}
                        {showActiveLine && (
                            <g data-testid="lc-active-line">
                                <line x1={ax1} y1={ay1} x2={ax2} y2={ay2} stroke="#f59e0b" strokeWidth={18} strokeOpacity={0.10} strokeLinecap="round" />
                                <line x1={ax1} y1={ay1} x2={ax2} y2={ay2} stroke="#fbbf24" strokeWidth={9} strokeOpacity={0.22} strokeLinecap="round" />
                                <line x1={ax1} y1={ay1} x2={ax2} y2={ay2} stroke="#fde047" strokeWidth={3} strokeLinecap="round" />
                            </g>
                        )}
                        {hasLine && (
                            <g data-testid="lc-line">
                                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#f59e0b" strokeWidth={18} strokeOpacity={0.14} strokeLinecap="round" />
                                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#fbbf24" strokeWidth={9} strokeOpacity={0.30} strokeLinecap="round" />
                                <line
                                  x1={x1}
                                  y1={y1}
                                  x2={x2}
                                  y2={y2}
                                  stroke="#fde047"
                                  strokeWidth={3.5}
                                  strokeLinecap="round"
                                  strokeDasharray={editMode || lineDirty ? '14 10' : undefined}
                                />
                            </g>
                        )}
                        {hasLine && (
                            <>
                                <line x1={baseX} y1={baseY} x2={tipX} y2={tipY} stroke="#f97316" strokeWidth={4.5} strokeLinecap="round" />
                                <polygon points={`${tipX},${tipY} ${h1x},${h1y} ${h2x},${h2y}`} fill="#f97316" />
                            </>
                        )}
                        {editMode && editPhase >= 1 && (
                            <>
                                <circle cx={x1} cy={y1} r={13} fill="none" stroke="#fde047" strokeWidth={2.5} strokeOpacity={0.5} />
                                <circle cx={x1} cy={y1} r={5} fill="#fde047" />
                            </>
                        )}
                        {editMode && hasLine && (
                            <>
                                <circle cx={x2} cy={y2} r={13} fill="none" stroke="#fde047" strokeWidth={2.5} />
                                <circle cx={x2} cy={y2} r={5} fill="#fde047" />
                                <circle cx={ox} cy={oy} r={9} fill="rgba(59,130,246,0.65)" stroke="#ffffff" strokeWidth={2} />
                            </>
                        )}
                    </svg>
                )}

                {badgeText && (
                    <div
                      className={`absolute top-2 left-2 z-10 text-white text-xs px-2.5 py-1 rounded backdrop-blur-sm ${state === 'running' ? 'bg-emerald-600/80' : state === 'disabled' ? 'bg-gray-600/80' : state === 'unsupported_model' ? 'bg-amber-600/80' : 'bg-rose-600/80'}`}
                      data-testid="lc-runtime-badge"
                    >
                        {badgeText}
                    </div>
                )}

                <Button
                  variant="outline"
                  size="sm"
                  className="absolute top-2 right-2 z-10 h-7 px-2 text-xs bg-black/50 hover:bg-black/70 border-white/20 text-white"
                  onClick={async () => {
                        const player = playerRef.current;
                        const url = videoUrlRef.current;
                        if (!player || !url) return;
                        setReloading(true);
                        try {
                            await deviceTool.startVideoStreamReq().catch(() => {});
                            player.resetStartState().start(url);
                        } finally {
                            setTimeout(() => setReloading(false), 800);
                        }
                    }}
                  disabled={reloading}
                >
                    <RefreshCw className={`w-3 h-3 mr-1 ${reloading ? 'animate-spin' : ''}`} />
                    {reloading ? '...' : ''}
                </Button>

                {connState === 'reconnecting' && (
                    <div className="absolute top-2 left-1/2 -translate-x-1/2 z-10 bg-black/70 text-amber-300 text-[11px] px-3 py-1 rounded backdrop-blur-sm pointer-events-none whitespace-nowrap animate-pulse">
                        {i18n._('sys.line_counting.reconnecting')}
                    </div>
                )}

                {hint && (
                    <div className="absolute bottom-2 left-1/2 -translate-x-1/2 z-10 bg-black/65 text-white text-[11px] px-3 py-1 rounded backdrop-blur-sm pointer-events-none whitespace-nowrap">
                        {hint}
                    </div>
                )}
            </div>
        </div>
    );
}
