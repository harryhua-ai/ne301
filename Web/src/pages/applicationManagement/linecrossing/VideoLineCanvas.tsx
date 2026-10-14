import { useCallback, useEffect, useRef, useState } from 'preact/hooks'
import type H264Player from '@/lib/MSE/h264Player'
import { useLingui } from '@lingui/react'
import { Button } from '@/components/ui/button'
import { RefreshCw } from 'lucide-react'
import deviceTool from '@/services/api/deviceTool'
import { getWebSocketUrl } from '@/utils'
import type { LineCrossingActiveConfig } from '@/services/api/appSession'

export interface LineDraft {
  x1: number
  y1: number
  x2: number
  y2: number
  outX: number
  outY: number
}

export function draftFromConfig(c: LineCrossingActiveConfig | null | undefined): LineDraft | null {
  if (!c || !c.line || !c.outside) return null
  const l = c.line
  const o = c.outside
  if (
    ![l.x1_permille, l.y1_permille, l.x2_permille, l.y2_permille, o.x_permille, o.y_permille].every(
      (v) => typeof v === 'number' && Number.isFinite(v),
    )
  ) {
    return null
  }
  return {
    x1: l.x1_permille as number,
    y1: l.y1_permille as number,
    x2: l.x2_permille as number,
    y2: l.y2_permille as number,
    outX: o.x_permille as number,
    outY: o.y_permille as number,
  }
}

export function draftToLineFields(d: LineDraft) {
  return {
    line: { x1: d.x1, y1: d.y1, x2: d.x2, y2: d.y2 },
    outside: { x: d.outX, y: d.outY },
  }
}

function mirrorAcrossLine(d: LineDraft): LineDraft {
  const dx = d.x2 - d.x1
  const dy = d.y2 - d.y1
  const len2 = dx * dx + dy * dy
  if (len2 === 0) return d
  const px = d.outX - d.x1
  const py = d.outY - d.y1
  const t = (px * dx + py * dy) / len2
  const projX = d.x1 + t * dx
  const projY = d.y1 + t * dy
  const clamp = (v: number) => Math.max(0, Math.min(1000, Math.round(v)))
  return {
    ...d,
    outX: clamp(2 * projX - d.outX),
    outY: clamp(2 * projY - d.outY),
  }
}

export interface VideoLineCanvasStats {
  winIn: number | null
  winOut: number | null
  cumIn: number | null
  cumOut: number | null
}

interface VideoLineCanvasProps {
  draft: LineDraft | null
  onDraftChange: (d: LineDraft) => void
  invalidTarget: boolean
  stats?: VideoLineCanvasStats | null
}

const MAX_HARD_RESTARTS = 3

export default function VideoLineCanvas({ draft, onDraftChange, invalidTarget, stats }: VideoLineCanvasProps) {
  const { i18n } = useLingui()
  const playerRef = useRef<H264Player | null>(null)
  const videoRef = useRef<HTMLVideoElement>(null)
  const stageRef = useRef<HTMLDivElement>(null)
  const videoUrlRef = useRef<string>('')
  const restartCountRef = useRef(0)
  const lastTimeRef = useRef(0)
  const stallRef = useRef(0)
  const [drawMode, setDrawMode] = useState(false)
  const [clicks, setClicks] = useState<Array<{ x: number; y: number }>>([])
  const [videoFailed, setVideoFailed] = useState(false)
  const [reloading, setReloading] = useState(false)
  const [size, setSize] = useState({ w: 0, h: 0 })
  const [aspect, setAspect] = useState(16 / 9)
  const [connState, setConnState] = useState<'ok' | 'reconnecting'>('ok')
  const [retryCount, setRetryCount] = useState(0)

  useEffect(() => {
    if (typeof window === 'undefined') return
    let cancelled = false
    const videoUrl = getWebSocketUrl()
    videoUrlRef.current = videoUrl
    const start = async () => {
      try {
        await deviceTool.startVideoStreamReq()
      } catch {
        setVideoFailed(true)
        return
      }
      if (cancelled) return
      const video = videoRef.current
      if (!video) return
      try {
        const { default: PlayerCtor } = await import('@/lib/MSE/h264Player')
        if (cancelled) return
        const player = new PlayerCtor((msg) => {
          if (msg.t === 'mseError') {
            const count = ++restartCountRef.current
            if (count <= MAX_HARD_RESTARTS) {
              setTimeout(() => {
                playerRef.current?.hardRestart()
              }, 500)
            }
          }
        })
        playerRef.current = player
        player.initPlayer(video)
        player.start(videoUrl)
      } catch {
        setVideoFailed(true)
      }
    }
    start()
    return () => {
      cancelled = true
      try {
        playerRef.current?.destroy()
      } catch {
        setVideoFailed(true)
      }
      playerRef.current = null
      deviceTool.stopVideoStreamReq().catch(() => {})
    }
  }, [])

  useEffect(() => {
    const id = window.setInterval(() => {
      const v = videoRef.current
      if (!v) return
      if (v.currentTime === lastTimeRef.current) {
        if (lastTimeRef.current > 0 && ++stallRef.current >= 1) {
          stallRef.current = 0
          playerRef.current?.hardRestart()
        }
      } else {
        stallRef.current = 0
        lastTimeRef.current = v.currentTime
      }
    }, 1000)
    return () => window.clearInterval(id)
  }, [])

  useEffect(() => {
    const BACKOFF = [300, 1000, 2000, 4000, 8000, 15000]
    let step = 0
    let timer: ReturnType<typeof setTimeout> | null = null
    let active = false

    const tryReconnect = () => {
      const player = playerRef.current
      if (player) player.hardRestart()
      setRetryCount((n) => n + 1)
      const delay = BACKOFF[Math.min(step, BACKOFF.length - 1)]
      step += 1
      timer = setTimeout(tryReconnect, delay)
    }

    const onClose = () => {
      if (active) return
      active = true
      setConnState('reconnecting')
      setRetryCount(0)
      step = 1
      timer = setTimeout(tryReconnect, BACKOFF[0])
    }

    const onOpen = () => {
      active = false
      step = 0
      if (timer) {
        clearTimeout(timer)
        timer = null
      }
      setRetryCount(0)
      setConnState('ok')
    }

    window.addEventListener('wv_close', onClose)
    window.addEventListener('wv_open', onOpen)
    return () => {
      window.removeEventListener('wv_close', onClose)
      window.removeEventListener('wv_open', onOpen)
      if (timer) clearTimeout(timer)
    }
  }, [])

  useEffect(() => {
    const el = stageRef.current
    if (!el) return
    const update = () => setSize({ w: el.clientWidth, h: el.clientHeight })
    update()
    const ro = new ResizeObserver(update)
    ro.observe(el)
    return () => ro.disconnect()
  }, [])

  const handleReload = async () => {
    const player = playerRef.current
    const url = videoUrlRef.current
    if (!player || !url) return
    setReloading(true)
    try {
      await deviceTool.startVideoStreamReq().catch(() => {})
      restartCountRef.current = 0
      player.resetStartState().start(url)
    } finally {
      setTimeout(() => setReloading(false), 800)
    }
  }

  const handleStageClick = useCallback(
    (e: MouseEvent) => {
      if (!drawMode) return
      const el = stageRef.current
      if (!el) return
      const rect = el.getBoundingClientRect()
      if (rect.width === 0 || rect.height === 0) return
      const x = Math.max(0, Math.min(1000, Math.round(((e.clientX - rect.left) / rect.width) * 1000)))
      const y = Math.max(0, Math.min(1000, Math.round(((e.clientY - rect.top) / rect.height) * 1000)))
      if (clicks.length === 0) {
        setClicks([{ x, y }])
        return
      }
      const first = clicks[0]
      if (first.x === x && first.y === y) {
        setClicks([])
        return
      }
      const base = draft || { x1: first.x, y1: first.y, x2: x, y2: y, outX: first.x, outY: first.y }
      const next: LineDraft = {
        x1: first.x,
        y1: first.y,
        x2: x,
        y2: y,
        outX: base.outX,
        outY: base.outY,
      }
      const clamped: LineDraft = {
        ...next,
        outX: Math.max(0, Math.min(1000, next.outX)),
        outY: Math.max(0, Math.min(1000, next.outY)),
      }
      onDraftChange(mirrorAcrossLine(clamped))
      setClicks([])
      setDrawMode(false)
    },
    [drawMode, clicks, draft, onDraftChange],
  )

  const { w, h } = size
  const hasDraft = draft !== null
  const showOverlay = w > 0 && h > 0
  const p2px = (v: number, dim: number) => (v / 1000) * dim
  const x1 = draft ? p2px(draft.x1, w) : 0
  const y1 = draft ? p2px(draft.y1, h) : 0
  const x2 = draft ? p2px(draft.x2, w) : 0
  const y2 = draft ? p2px(draft.y2, h) : 0
  const ox = draft ? p2px(draft.outX, w) : 0
  const oy = draft ? p2px(draft.outY, h) : 0
  const mx = (x1 + x2) / 2
  const my = (y1 + y2) / 2
  const ang = Math.atan2(oy - my, ox - mx)
  const arrowSize = Math.max(14, w * 0.03)
  const a1x = mx + Math.cos(ang) * arrowSize
  const a1y = my + Math.sin(ang) * arrowSize
  const a2x = mx + Math.cos(ang + 2.5) * arrowSize
  const a2y = my + Math.sin(ang + 2.5) * arrowSize
  const a3x = mx + Math.cos(ang - 2.5) * arrowSize
  const a3y = my + Math.sin(ang - 2.5) * arrowSize
  const showStats = stats !== null && stats !== undefined && !drawMode

  return (
    <div
      className="relative flex min-h-0 w-full flex-1 items-center justify-center overflow-hidden bg-black"
      data-testid="lc-video"
    >
      <div
        ref={stageRef}
        className={`relative w-full ${drawMode ? 'cursor-crosshair' : ''}`}
        onClick={handleStageClick}
      >
        <video
          ref={videoRef}
          className="block h-auto w-full"
          style={{ aspectRatio: String(aspect) }}
          muted
          playsInline
          autoPlay
          disableRemotePlayback
          data-testid="lc-video-el"
          onLoadedMetadata={(e) => {
            const v = e.currentTarget
            if (v.videoWidth && v.videoHeight) setAspect(v.videoWidth / v.videoHeight)
          }}
        />
        {showOverlay && (
          <svg
            className="pointer-events-none absolute inset-0 h-full w-full"
            viewBox={`0 0 ${w} ${h}`}
            data-testid="lc-canvas"
          >
            {hasDraft && (
              <>
                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#ffe569" strokeWidth={22} strokeOpacity={0.14} strokeLinecap="round" />
                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#ffe569" strokeWidth={11} strokeOpacity={0.3} strokeLinecap="round" />
                <line x1={x1} y1={y1} x2={x2} y2={y2} stroke="#ffe569" strokeWidth={3.5} strokeLinecap="round" />
              </>
            )}
            {hasDraft && (
              <>
                <line x1={mx} y1={my} x2={ox} y2={oy} stroke="#f86f12" strokeWidth={2} strokeDasharray="6 6" />
                <polygon points={`${a1x},${a1y} ${a2x},${a2y} ${a3x},${a3y}`} fill="#f86f12" />
                <circle cx={ox} cy={oy} r={6} fill="#f86f12" />
                <circle cx={x1} cy={y1} r={10} fill="none" stroke="#ffe569" strokeWidth={2.5} strokeOpacity={0.5} />
                <circle cx={x1} cy={y1} r={4} fill="#ffe569" />
                <circle cx={x2} cy={y2} r={10} fill="none" stroke="#ffe569" strokeWidth={2.5} strokeOpacity={0.5} />
                <circle cx={x2} cy={y2} r={4} fill="#ffe569" />
              </>
            )}
            {clicks.map((c, i) => (
              <circle key={i} cx={p2px(c.x, w)} cy={p2px(c.y, h)} r={7} fill="#f24276" />
            ))}
          </svg>
        )}
        {showStats && (
          <div
            className="absolute bottom-2 left-2 z-10 space-y-0.5 rounded bg-black/60 px-3 py-1.5 font-mono text-sm text-white backdrop-blur-sm"
            data-testid="lc-video-stats"
          >
            <div>
              <span className="font-bold text-green-400">IN</span>
              <span className="ml-2">{stats?.winIn ?? 0}</span>
              <span className="ml-3 text-green-400/60">({stats?.cumIn ?? 0})</span>
            </div>
            <div>
              <span className="font-bold text-red-400">OUT</span>
              <span className="ml-2">{stats?.winOut ?? 0}</span>
              <span className="ml-3 text-red-400/60">({stats?.cumOut ?? 0})</span>
            </div>
          </div>
        )}
        <span
          className={`absolute left-2 top-2 rounded px-2 py-0.5 text-xs font-semibold text-white ${invalidTarget ? 'bg-[#f24276]' : 'bg-black/40'}`}
          data-testid="lc-target-chip"
        >
          {invalidTarget ? i18n._('sys.app_management.business.target_invalid') : i18n._('sys.app_management.business.target_chip')}
        </span>
        <Button
          variant="outline"
          size="sm"
          className="absolute right-2 top-2 z-10 h-7 bg-black/50 px-2 text-xs text-white hover:bg-black/70"
          onClick={handleReload}
          disabled={reloading}
          data-testid="lc-video-refresh"
        >
          <RefreshCw className={`h-3 w-3 ${reloading ? 'animate-spin' : ''}`} />
        </Button>
        {connState === 'reconnecting' && (
          <div
            className="absolute left-1/2 top-2 z-10 -translate-x-1/2 whitespace-nowrap rounded bg-black/70 px-3 py-1 text-[11px] text-amber-300 backdrop-blur-sm"
            data-testid="lc-video-reconnecting"
          >
            {i18n._('sys.app_management.business.reconnecting')}({retryCount})
          </div>
        )}
        {videoFailed && (
          <span
            className="absolute bottom-2 right-2 rounded bg-black/60 px-2 py-1 text-xs text-white"
            data-testid="lc-video-failed"
          >
            {i18n._('sys.app_management.business.video_unavailable')}
          </span>
        )}
        {drawMode && (
          <div
            className="absolute bottom-2 left-1/2 z-10 -translate-x-1/2 whitespace-nowrap rounded bg-black/65 px-3 py-1 text-[11px] text-white backdrop-blur-sm"
            data-testid="lc-draw-hint-overlay"
          >
            {clicks.length === 0
              ? i18n._('sys.app_management.business.draw_hint_first')
              : i18n._('sys.app_management.business.draw_hint_second')}
          </div>
        )}
      </div>
      <div className="flex shrink-0 flex-wrap gap-2 p-2">
        <Button
          variant={drawMode ? 'default' : 'outline'}
          size="sm"
          className={drawMode ? 'text-primary-foreground' : undefined}
          onClick={() => {
            setDrawMode((v) => !v)
            setClicks([])
          }}
          data-testid="lc-draw-toggle"
        >
          {i18n._('sys.app_management.business.draw_line')}
        </Button>
        <Button
          variant="outline"
          size="sm"
          disabled={!draft}
          onClick={() => draft && onDraftChange(mirrorAcrossLine(draft))}
          data-testid="lc-swap-direction"
        >
          {i18n._('sys.app_management.business.swap_direction')}
        </Button>
        {drawMode && (
          <span className="self-center text-xs text-[#718093]" data-testid="lc-draw-hint">
            {clicks.length === 0
              ? i18n._('sys.app_management.business.draw_hint_first')
              : i18n._('sys.app_management.business.draw_hint_second')}
          </span>
        )}
      </div>
    </div>
  )
}
