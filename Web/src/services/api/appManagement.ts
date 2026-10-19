import request from '../request'
import type { SidecarBridgeCallResponse, SidecarGrant, SidecarOp, SidecarUiProjection } from '@/types/sidecar'

export type AppCommittedState = 'installed' | 'unavailable' | 'none' | 'unknown'

export interface AppIdentity {
  publisher_id?: string
  app_id?: string
  version_major?: number
  version_minor?: number
  version_patch?: number
  package_len?: number
  package_sha256?: string
  tbs_sha256?: string
}

export interface AppStateResponse extends AppIdentity {
  state?: string
  status?: string
  reason?: string
  executable?: boolean
  app_running?: boolean
}

export interface AppOperationResponse extends AppIdentity {
  status?: string
  reason?: string
  detail?: string
  executable?: boolean
  app_running?: boolean
  entry_returned?: boolean
  entry_result?: number
  log?: string
  log_truncated?: boolean
}

export interface ApiEnvelope<T = unknown> {
  success?: boolean
  error_code?: string
  message?: string
  data?: T
}

export const APP_PACKAGE_MAX_BYTES = 2097400

const quietConfig = { skipErrorToast: true } as any

const appManagementApi = {
  getInstalledState: () => request.get('/api/v1/app/installed'),
  installPackage: (packageBase64: string) => request.post('/api/v1/app/install', { package_base64: packageBase64 }, quietConfig),
  uninstallApp: () => request.delete('/api/v1/app/installed', quietConfig),
  runApp: () => request.post('/api/v1/app/run', {}, quietConfig),
  getSidecarUi: () => request.get('/api/v1/app/sidecar/ui', quietConfig),
  requestSidecarGrant: (capabilities: SidecarOp[]) => request.post('/api/v1/app/sidecar/bridge/grant', { capabilities }, quietConfig),
  sidecarBridgeCall: (
    op: SidecarOp,
    body: {
      grant_id: string
      request_id: number
      payload?: unknown
      expected_config_generation?: number
      confirm_token?: string
    },
  ) => request.post('/api/v1/app/sidecar/bridge', { op, ...body }, quietConfig),
  confirmSidecarDangerous: (grantId: string, op: SidecarOp) => request.post('/api/v1/app/sidecar/bridge/confirm', { grant_id: grantId, op }, quietConfig),
  revokeSidecarGrants: (grantId?: string) => request.post(
      '/api/v1/app/sidecar/bridge/revoke',
      grantId ? { grant_id: grantId } : { all: true },
      quietConfig,
    ),
}

export type { SidecarBridgeCallResponse, SidecarGrant, SidecarUiProjection }

export function fileToBase64(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader()
    reader.onerror = () => reject(reader.error)
    reader.onload = () => {
      const result = String(reader.result || '')
      const comma = result.indexOf(',')
      resolve(comma >= 0 ? result.slice(comma + 1) : result)
    }
    reader.readAsDataURL(file)
  })
}

export default appManagementApi
