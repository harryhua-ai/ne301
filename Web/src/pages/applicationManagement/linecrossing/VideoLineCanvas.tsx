import { useEffect, useRef, useState } from 'preact/hooks'
import type H264Player from '@/lib/MSE/h264Player'
import { useLingui } from '@lingui/react'
import { Button } from '@/components/ui/button'
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

interface VideoLineCanvasProps {
  draft: LineDraft | null
  onDraftChange: (d: LineDraft) => void
  invalidTarget: boolean
}

export default function VideoLineCanvas({ draft, onDraftChange, invalidTarget }: VideoLineCanvasProps) {
  const { i18n } = useLingui()
  const videoRef = useRef<HTMLVideoElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const playerRef = useRef<H264Player | null>(null)
  const [drawMode, setDrawMode] = useState(false)
  const [clicks, setClicks] = useState<Array<{ x: number; y: number }>>([])
  const [videoFailed, setVideoFailed] = useState(false)

  useEffect(() => {
    if (typeof window === 'undefined') return
    let cancelled = false
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
        const player = new PlayerCtor(() => {})
        playerRef.current = player
        player.initPlayer(video)
        player.start(getWebSocketUrl())
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

  const drawOverlay = () => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return
    const w = canvas.width
    const h = canvas.height
    ctx.clearRect(0, 0, w, h)
    const line = draft
    if (!line) return
    const px = (v: number) => (v / 1000) * w
    const py = (v: number) => (v / 1000) * h
    ctx.lineWidth = Math.max(3, w * 0.004)
    ctx.strokeStyle = '#ffe569'
    ctx.shadowColor = '#ffe569'
    ctx.shadowBlur = 6
    ctx.beginPath()
    ctx.moveTo(px(line.x1), py(line.y1))
    ctx.lineTo(px(line.x2), py(line.y2))
    ctx.stroke()
    ctx.shadowBlur = 0
    const mx = px((line.x1 + line.x2) / 2)
    const my = py((line.y1 + line.y2) / 2)
    const ox = px(line.outX)
    const oy = py(line.outY)
    const ang = Math.atan2(oy - my, ox - mx)
    const size = Math.max(14, w * 0.03)
    ctx.fillStyle = '#f86f12'
    ctx.beginPath()
    ctx.moveTo(mx + Math.cos(ang) * size, my + Math.sin(ang) * size)
    ctx.lineTo(mx + Math.cos(ang + 2.5) * size, my + Math.sin(ang + 2.5) * size)
    ctx.lineTo(mx + Math.cos(ang - 2.5) * size, my + Math.sin(ang - 2.5) * size)
    ctx.closePath()
    ctx.fill()
    ctx.strokeStyle = '#f86f12'
    ctx.setLineDash([6, 6])
    ctx.lineWidth = 2
    ctx.beginPath()
    ctx.moveTo(mx, my)
    ctx.lineTo(ox, oy)
    ctx.stroke()
    ctx.setLineDash([])
    ctx.fillStyle = '#f86f12'
    ctx.beginPath()
    ctx.arc(ox, oy, 5, 0, Math.PI * 2)
    ctx.fill()
    if (clicks.length > 0) {
      ctx.fillStyle = '#f24276'
      for (const c of clicks) {
        ctx.beginPath()
        ctx.arc(px(c.x), py(c.y), 6, 0, Math.PI * 2)
        ctx.fill()
      }
    }
  }

  useEffect(() => {
    drawOverlay()
  })

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const sync = () => {
      const video = videoRef.current
      if (!video) return
      const rect = video.getBoundingClientRect()
      if (rect.width > 0 && canvas.width !== Math.round(rect.width)) {
        canvas.width = Math.round(rect.width)
        canvas.height = Math.round(rect.height)
      }
      drawOverlay()
    }
    sync()
    window.addEventListener('resize', sync)
    const timer = window.setInterval(sync, 800)
    return () => {
      window.removeEventListener('resize', sync)
      window.clearInterval(timer)
    }
  }, [draft, clicks])

  const handleCanvasClick = (e: MouseEvent) => {
    if (!drawMode) return
    const canvas = canvasRef.current
    if (!canvas) return
    const rect = canvas.getBoundingClientRect()
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
  }

  return (
    <div className="relative w-full overflow-hidden rounded-md border border-[#d5e1e4] bg-[#596f75]" data-testid="lc-video">
      <div className="relative w-full" style={{ aspectRatio: '16 / 9' }}>
        <video ref={videoRef} className="block h-full w-full object-cover" muted playsInline autoPlay data-testid="lc-video-el" />
        <canvas
          ref={canvasRef}
          className={`absolute inset-0 h-full w-full ${drawMode ? 'cursor-crosshair' : 'pointer-events-none'}`}
          onClick={handleCanvasClick}
          data-testid="lc-canvas"
        />
        <span
          className={`absolute left-2 top-2 rounded px-2 py-0.5 text-xs font-semibold text-white ${invalidTarget ? 'bg-[#f24276]' : 'bg-black/40'}`}
          data-testid="lc-target-chip"
        >
          {invalidTarget ? i18n._('sys.app_management.business.target_invalid') : i18n._('sys.app_management.business.target_chip')}
        </span>
        {videoFailed && (
          <span
            className="absolute bottom-2 left-2 rounded bg-black/60 px-2 py-1 text-xs text-white"
            data-testid="lc-video-failed"
          >
            {i18n._('sys.app_management.business.video_unavailable')}
          </span>
        )}
      </div>
      <div className="flex flex-wrap gap-2 p-2">
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
