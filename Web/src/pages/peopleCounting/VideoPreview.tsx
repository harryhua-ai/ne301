import { useEffect, useRef, useState, useCallback } from 'preact/hooks';
import { useLingui } from '@lingui/react';
import H264Player from '@/lib/MSE/h264Player';
import deviceTool from '@/services/api/deviceTool';
import { getWebSocketUrl } from '@/utils';
import { Button } from '@/components/ui/button';
import { RefreshCw } from 'lucide-react';
import type { PeopleCountingConfig, PeopleCountingStats } from '@/services/api/peopleCounting';

export interface VideoPreviewProps {
    config: PeopleCountingConfig | null;
    stats: PeopleCountingStats | null;
    editMode: boolean;
    editPhase: 0 | 1 | 2;
    onPickPoint: (permilleX: number, permilleY: number) => void;
}

/**
 * Live H264 preview + counting-line overlay + line editing, all in one
 * coordinate space (the video's rendered box) so what you draw is exactly
 * what is counted — no orientation/offset mismatch.
 *
 * The video is rendered at its natural aspect (w-full h-auto, no letterbox),
 * and the SVG overlay uses a 1:1 pixel viewBox (via ResizeObserver) so line
 * thickness, endpoint rings and the direction arrow never distort.
 */
export default function VideoPreview({ config, stats, editMode, editPhase, onPickPoint }: VideoPreviewProps) {
    const { i18n } = useLingui();
    const playerRef = useRef<H264Player | null>(null);
    const videoRef = useRef<HTMLVideoElement>(null);
    const stageRef = useRef<HTMLDivElement>(null);   // wraps the video — defines the coord space
    const videoUrlRef = useRef<string>('');
    const restartCountRef = useRef(0);
    const [reloading, setReloading] = useState(false);
    const [size, setSize] = useState({ w: 0, h: 0 });
    const [aspect, setAspect] = useState(16 / 9);   // locked from video metadata — keeps the frame box stable when the stream goes black
    const lastTimeRef = useRef(0);
    const stallRef = useRef(0);
    const [connState, setConnState] = useState<'ok' | 'reconnecting'>('ok');
    const [retryCount, setRetryCount] = useState(0);

    // ---- Player setup (unchanged) ----
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
                            console.warn(`[VideoPreview] mseError, hardRestart attempt ${count}/3`);
                            setTimeout(() => { playerRef.current?.hardRestart(); }, 500);
                        } else {
                            console.error('[VideoPreview] mseError, max restarts reached');
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

    // ---- Watchdog: if the video freezes (black/stuck without an mseError), hardRestart ----
    useEffect(() => {
        const id = setInterval(() => {
            const v = videoRef.current;
            if (!v) return;
            if (v.currentTime === lastTimeRef.current) {
                if (lastTimeRef.current > 0 && ++stallRef.current >= 1) {   // ~1s with no progress (after playback started)
                    stallRef.current = 0;
                    console.warn('[VideoPreview] video stalled >1s, hardRestart');
                    playerRef.current?.hardRestart();
                }
            } else {
                stallRef.current = 0;
                lastTimeRef.current = v.currentTime;
            }
        }, 1000);
        return () => clearInterval(id);
    }, []);

    // ---- WebSocket disconnect → backoff reconnect ----
    // The H264Player only auto-reconnects on WS 'error' (limited retries), NOT
    // on 'close' — but server-side / network drops usually surface as 'close',
    // which would leave the preview black forever. Re-arm a backoff reconnect
    // on close; any path that succeeds dispatches 'open' and cancels the loop.
    useEffect(() => {
        const BACKOFF = [300, 1000, 2000, 4000, 8000, 15000];
        let step = 0;
        let timer: ReturnType<typeof setTimeout> | null = null;
        let active = false;

        const tryReconnect = () => {
            const player = playerRef.current;
            if (player) player.hardRestart();
            setRetryCount((n) => n + 1);
            const delay = BACKOFF[Math.min(step, BACKOFF.length - 1)];
            step += 1;
            timer = setTimeout(tryReconnect, delay);
        };

        const onClose = () => {
            if (active) return;          // already in a reconnect loop
            active = true;
            setConnState('reconnecting');
            setRetryCount(0);
            step = 1;
            timer = setTimeout(tryReconnect, BACKOFF[0]);
        };

        const onOpen = () => {
            active = false;
            step = 0;
            if (timer) { clearTimeout(timer); timer = null; }
            setRetryCount(0);
            setConnState('ok');
        };

        window.addEventListener('wv_close', onClose);
        window.addEventListener('wv_open', onOpen);
        return () => {
            window.removeEventListener('wv_close', onClose);
            window.removeEventListener('wv_open', onOpen);
            if (timer) clearTimeout(timer);
        };
    }, []);

    // ---- Track stage size → overlay viewBox matches the video 1:1 (no distortion) ----
    useEffect(() => {
        const el = stageRef.current;
        if (!el) return;
        const update = () => setSize({ w: el.clientWidth, h: el.clientHeight });
        update();
        const ro = new ResizeObserver(update);
        ro.observe(el);
        return () => ro.disconnect();
    }, []);

    const handleReload = async () => {
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
    };

    // ---- Click → permille, relative to the stage (= the video box) → WYSIWYG ----
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

    // ---- Geometry (permille → pixels of the current stage) ----
    const { w, h } = size;
    const hasLine = !!config && (
        config.line_x1_permille !== config.line_x2_permille
        || config.line_y1_permille !== config.line_y2_permille
    );
    const showOverlay = (config?.enable || editMode) && w > 0 && h > 0;
    const p2px = (permille: number, dim: number) => (permille / 1000) * dim;
    const x1 = config ? p2px(config.line_x1_permille, w) : 0;
    const y1 = config ? p2px(config.line_y1_permille, h) : 0;
    const x2 = config ? p2px(config.line_x2_permille, w) : 0;
    const y2 = config ? p2px(config.line_y2_permille, h) : 0;
    const ox = config ? p2px(config.outside_x_permille, w) : 0;
    const oy = config ? p2px(config.outside_y_permille, h) : 0;

    // direction arrow points toward "inside" (midpoint − outside), drawn from near the midpoint
    const mx = (x1 + x2) / 2; const 
my = (y1 + y2) / 2;
    let dx = mx - ox; let 
dy = my - oy;
    const dlen = Math.hypot(dx, dy) || 1; dx /= dlen; dy /= dlen;
    const aLen = Math.max(30, Math.min(w, h) * 0.07);
    const tipX = mx + dx * aLen; const 
tipY = my + dy * aLen;
    const baseX = mx + dx * aLen * 0.3; const 
baseY = my + dy * aLen * 0.3;
    const ang = Math.atan2(dy, dx);
    const head = 13;
    const h1x = tipX - head * Math.cos(ang - Math.PI / 6); const 
h1y = tipY - head * Math.sin(ang - Math.PI / 6);
    const h2x = tipX - head * Math.cos(ang + Math.PI / 6); const 
h2y = tipY - head * Math.sin(ang + Math.PI / 6);

    // progressive visibility during editing
    const showL1 = editMode ? editPhase >= 1 : showOverlay;
    const showLine = hasLine;
    const showL2 = showLine;
    const showOutside = editMode ? editPhase >= 2 : showOverlay;
    const showArrow = showLine && (editMode ? editPhase >= 2 : true);

    const hintKey = !editMode ? null
        : editPhase === 0 ? 'sys.pc.hint_click_l1'
        : editPhase === 1 ? 'sys.pc.hint_click_l2'
        : 'sys.pc.hint_done';

    return (
        <div className="relative w-full h-full flex items-center justify-center min-h-0 bg-black">
            <div
              ref={stageRef}
              className={`relative w-full ${editMode ? 'cursor-crosshair' : ''}`}
              onClick={handleStageClick}
            >
                <video
                  ref={videoRef}
                  className="block w-full h-auto"
                  id="pcVideoPlayer"
                  style={{ aspectRatio: String(aspect) }}
                  muted
                  playsInline
                  disableRemotePlayback
                  onLoadedMetadata={(e) => {
                        const v = e.currentTarget;
                        if (v.videoWidth && v.videoHeight) setAspect(v.videoWidth / v.videoHeight);
                    }}
                />

                {/* Glow counting-line overlay (1:1 pixel viewBox → no distortion) */}
                {showOverlay && (
                    <svg className="absolute inset-0 w-full h-full pointer-events-none" viewBox={`0 0 ${w} ${h}`}>
                        {showLine && (
                            <>
                                {/* outer glow → mid glow → crisp core */}
                                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#f59e0b" strokeWidth={22} strokeOpacity={0.14} strokeLinecap="round" />
                                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#fbbf24" strokeWidth={11} strokeOpacity={0.30} strokeLinecap="round" />
                                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#fde047" strokeWidth={3.5} strokeLinecap="round" />
                            </>
                        )}
                        {showArrow && (
                            <>
                                <line x1={baseX} y1={baseY} x2={tipX} y2={tipY} stroke="#f97316" strokeWidth={4.5} strokeLinecap="round" />
                                <polygon points={`${tipX},${tipY} ${h1x},${h1y} ${h2x},${h2y}`} fill="#f97316" />
                            </>
                        )}
                        {showL1 && (
                            <>
                                <circle cx={x1} cy={y1} r={13} fill="none" stroke="#fde047" strokeWidth={2.5} strokeOpacity={0.5} />
                                <circle cx={x1} cy={y1} r={12} fill="none" stroke="#fde047" strokeWidth={2.5} />
                                <circle cx={x1} cy={y1} r={4.5} fill="#fde047" />
                            </>
                        )}
                        {showL2 && (
                            <>
                                <circle cx={x2} cy={y2} r={13} fill="none" stroke="#fde047" strokeWidth={2.5} strokeOpacity={0.5} />
                                <circle cx={x2} cy={y2} r={12} fill="none" stroke="#fde047" strokeWidth={2.5} />
                                <circle cx={x2} cy={y2} r={4.5} fill="#fde047" />
                            </>
                        )}
                        {showOutside && (
                            <circle cx={ox} cy={oy} r={9} fill="rgba(59,130,246,0.65)" stroke="#ffffff" strokeWidth={2} />
                        )}
                    </svg>
                )}

                {/* IN/OUT stats — top-left (only when enabled, not while editing) */}
                {config?.enable && !editMode && (
                    <div className="absolute top-2 left-2 z-10 bg-black/60 text-white px-3 py-1.5 rounded text-sm font-mono space-y-0.5 backdrop-blur-sm">
                        <div>
                            <span className="text-green-400 font-bold">IN</span>
                            <span className="ml-2">{stats?.window_in ?? 0}</span>
                            <span className="ml-3 text-green-400/60">({stats?.total_in ?? 0})</span>
                        </div>
                        <div>
                            <span className="text-red-400 font-bold">OUT</span>
                            <span className="ml-2">{stats?.window_out ?? 0}</span>
                            <span className="ml-3 text-red-400/60">({stats?.total_out ?? 0})</span>
                        </div>
                    </div>
                )}

                {/* Reload — top-right */}
                <Button
                  variant="outline"
                  size="sm"
                  className="absolute top-2 right-2 z-10 h-7 px-2 text-xs bg-black/50 hover:bg-black/70 border-white/20 text-white"
                  onClick={handleReload}
                  disabled={reloading}
                >
                    <RefreshCw className={`w-3 h-3 mr-1 ${reloading ? 'animate-spin' : ''}`} />
                    {reloading ? '...' : ''}
                </Button>

                {/* Reconnect status — top center (shown when the WS dropped) */}
                {connState === 'reconnecting' && (
                    <div className="absolute top-2 left-1/2 -translate-x-1/2 z-10 bg-black/70 text-amber-300 text-[11px] px-3 py-1 rounded backdrop-blur-sm pointer-events-none whitespace-nowrap animate-pulse">
                        {i18n._('sys.pc.reconnecting')}({retryCount})
                    </div>
                )}

                {/* Edit phase hint — bottom center */}
                {hintKey && (
                    <div className="absolute bottom-2 left-1/2 -translate-x-1/2 z-10 bg-black/65 text-white text-[11px] px-3 py-1 rounded backdrop-blur-sm pointer-events-none whitespace-nowrap">
                        {i18n._(hintKey)}
                    </div>
                )}
            </div>
        </div>
    );
}
