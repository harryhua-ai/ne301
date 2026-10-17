import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { useLingui } from '@lingui/react'
import { SidecarBroker, type VideoSessionState } from '@/services/sidecarBridge'
import appManagementApi from '@/services/api/appManagement'
import { useAppManagementStore } from '@/store/appManagement'
import type { SidecarUiProjection } from '@/types/sidecar'

interface SidecarAppFrameProps {
  projection: SidecarUiProjection
  appId: string
}

type BridgePhase = 'handshaking' | 'active' | 'revoked'

export default function SidecarAppFrame({ projection, appId }: SidecarAppFrameProps) {
  const { i18n } = useLingui()
  const appRunning = useAppManagementStore(
    (s) => (s.view?.kind === 'installed' ? s.view.appRunning === true : false),
  )
  const frameRef = useRef<HTMLIFrameElement | null>(null)
  const brokerRef = useRef<SidecarBroker | null>(null)
  const [phase, setPhase] = useState<BridgePhase>('handshaking')
  const [revokeReason, setRevokeReason] = useState<string | null>(null)
  const [videoState, setVideoState] = useState<VideoSessionState>('Unavailable')
  const [dangerousPending, setDangerousPending] = useState(false)
  const [confirmWorking, setConfirmWorking] = useState(false)
  const grant = useRef<{ grantId: string; capabilities: string[] } | null>(null)

  const appOrigin = projection.app_site_origin ?? ''
  const frameSrc = useMemo(() => {
    if (!appOrigin || !projection.ui_base_path) return ''
    const entry = projection.entry_path || 'index.html'
    return `${appOrigin}${projection.ui_base_path}${entry}`
  }, [appOrigin, projection.ui_base_path, projection.entry_path])

  useEffect(() => {
    let cancelled = false
    const teardown = () => {
      if (brokerRef.current) {
        brokerRef.current.revoke('FRAME_UNMOUNT')
        brokerRef.current = null
      }
    }
    const setup = async () => {
      if (!frameSrc || cancelled) return
      try {
        const res = (await appManagementApi.requestSidecarGrant([
          'snapshot.read',
          'config.read',
          'config.apply',
          'counters.reset',
          'report-policy.read',
          'report-policy.apply',
          'video.open',
          'video.close',
          'enable.read',
          'enable.apply',
          'live.subscribe',
          'live.unsubscribe',
        ])) as { success?: boolean; data?: { status?: string; grant?: { grant_id: string; capabilities: string[] } }; error_code?: string }
        if (cancelled) return
        if (!res || res.success !== true || !res.data || res.data.status !== 'granted' || !res.data.grant) {
          setPhase('revoked')
          setRevokeReason(res?.error_code || 'GRANT_REFUSED')
          return
        }
        const requested = res.data.grant
        grant.current = { grantId: requested.grant_id, capabilities: requested.capabilities }
        const iframeWindow = frameRef.current?.contentWindow
        if (!iframeWindow) {
          setPhase('revoked')
          setRevokeReason('IFRAME_WINDOW_UNAVAILABLE')
          return
        }
        const broker = new SidecarBroker({
          appWindow: iframeWindow,
          appOrigin: new URL(appOrigin).origin,
          grant: {
            grant_id: requested.grant_id,
            expires_at_ms: 0,
            issued_at_ms: 0,
            capabilities: requested.capabilities as never[],
            identity: {
              publisher_id: projection.publisher_id ?? '',
              app_id: projection.app_id ?? '',
              publisher_key_sha256: projection.publisher_key_sha256 ?? '',
              native_tbs_sha256: projection.native_tbs_sha256 ?? '',
              ui_tbs_sha256: projection.ui_tbs_sha256 ?? '',
              version_major: projection.version_major ?? 0,
              version_minor: projection.version_minor ?? 0,
              version_patch: projection.version_patch ?? 0,
              host_ui_abi: projection.host_ui_abi ?? 0,
              sidecar_service_generation: projection.sidecar_service_generation ?? '',
              host_boot_generation: projection.host_boot_generation ?? 0,
              native_session_generation: projection.native_session_generation ?? 0,
              ui_generation: projection.ui_generation ?? 0,
            },
          },
          serverCall: async (op, body) => {
            const response = (await appManagementApi.sidecarBridgeCall(op, body)) as {
              success?: boolean
              error_code?: string
              data?: { revoked?: boolean; result?: Record<string, unknown> }
            }
            if (!response || response.success !== true) {
              return {
                outcome: 'UNKNOWN' as const,
                reason: response?.error_code || 'TRANSPORT_FAILED',
                revoked: true,
              }
            }
            const data = response.data || {}
            const result = data.result || {}
            return {
              outcome: (result.outcome as never) ?? 'UNKNOWN',
              reason: result.reason as string | undefined,
              freshness: result.freshness as string | undefined,
              config_generation: result.config_generation as number | undefined,
              state_revision: result.state_revision as number | undefined,
              age_ms: result.age_ms as number | undefined,
              payload: result.payload,
              revoked: data.revoked === true,
            }
          },
          onResult: (result) => {
            if (
              result.op === 'counters.reset'
              && result.outcome === 'REJECTED'
              && result.reason === 'ADMIN_CONFIRM_REQUIRED'
            ) {
              setDangerousPending(true)
            }
            if (result.op === 'video.open') {
              if (result.outcome === 'APPLIED') broker.video.markLive()
              else if (result.outcome === 'UNKNOWN') broker.video.markReconnecting()
              else broker.video.markUnavailable()
            }
            if (result.op === 'video.close' && result.outcome !== 'UNAVAILABLE') {
              broker.video.markUnavailable()
            }
          },
          onRevoke: (reason) => {
            setPhase('revoked')
            setRevokeReason(reason)
          },
        })
        brokerRef.current = broker
        broker.video.subscribe((state) => setVideoState(state))
        broker.start()
        setPhase('active')
      } catch {
        if (!cancelled) {
          setPhase('revoked')
          setRevokeReason('GRANT_REQUEST_FAILED')
        }
      }
    }
    setup()
    return () => {
      cancelled = true
      teardown()
    }
  }, [frameSrc, appOrigin])

  const handleConfirmDangerous = async () => {
    const broker = brokerRef.current
    if (!broker || !dangerousPending || confirmWorking) return
    setConfirmWorking(true)
    const delivered = await broker.deliverDangerousConfirm('counters.reset', async (grantId, op) => {
      const res = (await appManagementApi.confirmSidecarDangerous(grantId, op)) as {
        success?: boolean
        data?: { confirm_token?: string }
      }
      if (!res || res.success !== true || !res.data?.confirm_token) {
        throw new Error('CONFIRM_REFUSED')
      }
      return res.data.confirm_token
    })
    setConfirmWorking(false)
    if (delivered) setDangerousPending(false)
  }

  const handleRevokeRetry = async () => {
    await useAppManagementStore.getState().refreshSidecar()
  }

  if (!frameSrc) {
    return (
      <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800" data-testid="sidecar-frame-blocked">
        {i18n._('sys.app_management.sidecar.origin_unavailable')}
      </div>
    )
  }

  return (
    <div>
      <div
        className="mb-2 flex flex-wrap items-center gap-x-3 gap-y-1 rounded border border-[#e6eaee] bg-[#f7f9fb] px-3 py-2 text-xs text-[#52616f]"
        data-testid="sidecar-status-strip"
      >
        <span>
          {i18n._('sys.app_management.sidecar.status_native')}:{' '}
          <span className="font-medium">{appRunning ? i18n._('sys.app_management.run.running') : i18n._('sys.app_management.run.idle')}</span>
        </span>
        <span>
          {i18n._('sys.app_management.sidecar.status_generation')}:{' '}
          <span className="font-mono" data-testid="sidecar-generation">{projection.sidecar_service_generation}</span>
        </span>
        <span>
          {i18n._('sys.app_management.sidecar.status_video')}:{' '}
          <span className="font-medium" data-testid="sidecar-video-state">{videoState}</span>
        </span>
        <span className="text-[#8b97a4]">
          {i18n._('sys.app_management.sidecar.status_note')}
        </span>
      </div>
      {phase === 'revoked' && (
        <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700" data-testid="sidecar-revoked">
          <p className="font-medium">{i18n._('sys.app_management.sidecar.revoked_title')}</p>
          <p className="mt-1 font-mono text-xs">{revokeReason}</p>
          <div className="mt-2 flex gap-2">
            <button
              type="button"
              className="rounded border border-[#dce3e9] bg-white px-2 py-1 text-xs hover:bg-[#f5f6f8]"
              onClick={() => {
                handleRevokeRetry()
              }}
              data-testid="sidecar-recheck"
            >
              {i18n._('sys.app_management.actions.refresh')}
            </button>
          </div>
        </div>
      )}
      {dangerousPending && phase === 'active' && (
        <div className="mb-2 rounded border border-[#f0d8be] bg-[#fff8f1] p-3 text-sm text-[#745638]" data-testid="sidecar-dangerous-confirm">
          <p>{i18n._('sys.app_management.sidecar.dangerous_confirm_note')}</p>
          <button
            type="button"
            className="mt-2 rounded border border-[#efcfce] bg-white px-2 py-1 text-xs font-semibold text-[#c44a48] hover:bg-[#fdf3f2] disabled:opacity-50"
            onClick={() => {
              handleConfirmDangerous()
            }}
            disabled={confirmWorking}
            data-testid="sidecar-dangerous-confirm-button"
          >
            {i18n._('sys.app_management.sidecar.dangerous_confirm_button')}
          </button>
        </div>
      )}
      {phase !== 'revoked' && (
        <iframe
          key={`${appOrigin}/${projection.sidecar_service_generation ?? ''}`}
          ref={frameRef}
          src={frameSrc}
          title={i18n._('sys.app_management.sidecar.tab_label')}
          sandbox="allow-scripts allow-same-origin allow-downloads"
          className="mt-2 h-[560px] w-full rounded-lg border border-[#e6eaee] bg-white"
          data-testid="sidecar-frame"
          data-app-id={appId}
        />
      )}
    </div>
  )
}
