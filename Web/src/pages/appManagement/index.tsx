import { useEffect, useState } from 'preact/hooks'
import { useLingui } from '@lingui/react'
import { Card, CardContent, CardHeader, CardTitle } from '@/components/ui/card'
import { Button } from '@/components/ui/button'
import { Separator } from '@/components/ui/separator'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/dialog'
import { useAppManagementStore, type AppLastError, type AppView } from '@/store/appManagement'
import { appEnvelopeKey, appFileErrorKey, appReasonKey } from './messages'

function IdentityRow({ label, value }: { label: string; value: string }) {
  return (
    <div className="flex justify-between gap-4 py-1 text-sm">
      <span className="shrink-0 text-text-secondary">{label}</span>
      <span className="break-all text-right font-mono">{value}</span>
    </div>
  )
}

function StateBadge({ view, i18n }: { view: AppView; i18n: { _: (k: string) => string } }) {
  const map: Record<AppView['kind'], { cls: string; key: string }> = {
    installed: { cls: 'bg-green-100 text-green-700', key: 'sys.app_management.state.installed' },
    unavailable: { cls: 'bg-orange-100 text-orange-700', key: 'sys.app_management.state.unavailable' },
    not_installed: { cls: 'bg-gray-100 text-gray-600', key: 'sys.app_management.state.not_installed' },
    undetermined: { cls: 'bg-red-100 text-red-700', key: 'sys.app_management.state.undetermined' },
  }
  const item = map[view.kind]
  return (
    <span className={`rounded px-2 py-0.5 text-xs font-medium ${item.cls}`}>
      {i18n._(item.key)}
    </span>
  )
}

function ErrorNotice({ error }: { error: AppLastError }) {
  const { i18n } = useLingui()
  if (error.kind === 'auth') {
    if (error.reason === 'unauthorized') {
      return (
        <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800">
          {i18n._('sys.app_management.auth.unauthorized')}
          <a className="ml-2 underline" href="/login">
            {i18n._('sys.app_management.auth.relogin')}
          </a>
        </div>
      )
    }
    return (
      <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800">
        {i18n._('sys.app_management.auth.forbidden')}
      </div>
    )
  }
  if (error.kind === 'refused') {
    return (
      <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700">
        {i18n._(appReasonKey(error.reason || ''))}
      </div>
    )
  }
  if (error.kind === 'envelope') {
    return (
      <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700">
        {i18n._(appEnvelopeKey(error.reason || ''))}
      </div>
    )
  }
  if (error.kind === 'indeterminate') {
    return (
      <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800">
        {i18n._('sys.app_management.errors.indeterminate')}
      </div>
    )
  }
  return (
    <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700">
      {i18n._(appFileErrorKey(error.reason))}
    </div>
  )
}

function IdentitySection({ view }: { view: AppView }) {
  const { i18n } = useLingui()
  if (view.kind !== 'installed' && view.kind !== 'unavailable') {
    return null
  }
  const { record } = view
  return (
    <div className="mt-2">
      <IdentityRow label={i18n._('sys.app_management.labels.publisher')} value={record.publisherId} />
      <IdentityRow label={i18n._('sys.app_management.labels.app_id')} value={record.appId} />
      <IdentityRow label={i18n._('sys.app_management.labels.version')} value={record.version} />
      <IdentityRow label={i18n._('sys.app_management.labels.tbs_sha256')} value={record.tbsSha256} />
      <IdentityRow
        label={i18n._('sys.app_management.labels.package_sha256')}
        value={`${record.packageSha256} (${i18n._('sys.app_management.labels.audit_only')})`}
      />
      <IdentityRow
        label={i18n._('sys.app_management.labels.package_len')}
        value={`${record.packageLen} ${i18n._('sys.app_management.labels.bytes')}`}
      />
      {view.kind === 'unavailable' && (
        <div className="mt-2 rounded border border-orange-300 bg-orange-50 p-3 text-sm text-orange-800">
          <p>{i18n._('sys.app_management.state.unavailable_note')}</p>
          <p className="mt-1 font-mono text-xs">{view.reason}</p>
        </div>
      )}
    </div>
  )
}

function RunOutcomePanel() {
  const { i18n } = useLingui()
  const runOutcome = useAppManagementStore((s) => s.runOutcome)
  if (!runOutcome) return null
  return (
    <div className="mt-3 rounded border border-gray-200 bg-gray-50 p-3 text-sm" data-testid="run-outcome">
      <p className="font-medium">{i18n._('sys.app_management.run.session_result')}</p>
      {runOutcome.status === 'completed' ? (
        <div className="mt-1 space-y-1">
          <p>
            {i18n._('sys.app_management.run.entry_returned')}: {String(runOutcome.entryReturned)}
          </p>
          <p>
            {i18n._('sys.app_management.run.entry_result')}: {runOutcome.entryResult ?? 'n/a'}
          </p>
          {runOutcome.log && (
            <pre className="max-h-40 overflow-auto whitespace-pre-wrap break-all rounded bg-white p-2 font-mono text-xs">
              {runOutcome.log}
            </pre>
          )}
          {runOutcome.logTruncated && (
            <p className="text-xs text-text-secondary">
              {i18n._('sys.app_management.run.log_truncated')}
            </p>
          )}
        </div>
      ) : (
        <div className="mt-1 space-y-1">
          <p>
            {i18n._('sys.app_management.run.host_refused')}: {i18n._(appReasonKey(runOutcome.reason || ''))}
          </p>
          {runOutcome.detail && <p className="font-mono text-xs">{runOutcome.detail}</p>}
        </div>
      )}
      <p className="mt-2 text-xs text-text-secondary">{i18n._('sys.app_management.run.session_note')}</p>
    </div>
  )
}

export default function AppManagement() {
  const { i18n } = useLingui()
  const view = useAppManagementStore((s) => s.view)
  const querying = useAppManagementStore((s) => s.querying)
  const pendingVerification = useAppManagementStore((s) => s.pendingVerification)
  const authBlock = useAppManagementStore((s) => s.authBlock)
  const installing = useAppManagementStore((s) => s.installing)
  const uninstalling = useAppManagementStore((s) => s.uninstalling)
  const running = useAppManagementStore((s) => s.running)
  const installOutcome = useAppManagementStore((s) => s.installOutcome)
  const lastError = useAppManagementStore((s) => s.lastError)
  const refresh = useAppManagementStore((s) => s.refresh)
  const installPackage = useAppManagementStore((s) => s.installPackage)
  const uninstallApp = useAppManagementStore((s) => s.uninstallApp)
  const runOnce = useAppManagementStore((s) => s.runOnce)
  const [selectedFile, setSelectedFile] = useState<File | null>(null)
  const [confirmOpen, setConfirmOpen] = useState(false)

  useEffect(() => {
    refresh()
  }, [refresh])

  const opBlocked = authBlock !== null || pendingVerification || view?.kind === 'undetermined'
  const canRun = view?.kind === 'installed' && !view.appRunning && !running && !opBlocked
  const canUninstall =
    (view?.kind === 'installed' || view?.kind === 'unavailable') && !opBlocked && !uninstalling
  const canInstall = !opBlocked && !installing

  const handleInstall = async () => {
    if (!selectedFile) return
    await installPackage(selectedFile)
    setSelectedFile(null)
    const input = document.getElementById('neapp-file-input') as HTMLInputElement | null
    if (input) input.value = ''
  }

  const handleUninstall = async () => {
    setConfirmOpen(false)
    await uninstallApp()
  }

  return (
    <div className="flex justify-center">
      <Card className="mx-4 my-4 w-full sm:w-xl">
        <CardHeader>
          <CardTitle>{i18n._('sys.app_management.title')}</CardTitle>
        </CardHeader>
        <CardContent>
          {authBlock !== null && (
            <div className="mb-3">
              <ErrorNotice error={{ scope: 'query', kind: 'auth', reason: authBlock }} />
            </div>
          )}
          {authBlock === null && view?.kind === 'undetermined' && (
            <div
              className="mb-3 rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700"
              data-testid="undetermined-warning"
            >
              <p>{i18n._('sys.app_management.state.undetermined_warning')}</p>
              {view.reason && <p className="mt-1 font-mono text-xs">{view.reason}</p>}
            </div>
          )}
          {pendingVerification && (
            <div
              className="mb-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800"
              data-testid="pending-verification"
            >
              {i18n._('sys.app_management.errors.pending_verification')}
            </div>
          )}
          {lastError && authBlock === null && (
            <div className="mb-3" data-testid="last-error">
              <ErrorNotice error={lastError} />
            </div>
          )}

          <div className="flex items-center justify-between">
            <div className="flex items-center gap-2">
              <span className="text-sm font-medium">{i18n._('sys.app_management.state.label')}</span>
              {view && !authBlock && <StateBadge view={view} i18n={i18n} />}
              {querying && (
                <span className="text-xs text-text-secondary">
                  {i18n._('sys.app_management.state.querying')}
                </span>
              )}
            </div>
            <Button variant="outline" size="sm" onClick={refresh} disabled={querying}>
              {i18n._('sys.app_management.actions.refresh')}
            </Button>
          </div>

          {view?.kind === 'not_installed' && (
            <p className="mt-2 text-sm text-text-secondary">
              {i18n._('sys.app_management.state.not_installed_note')}
            </p>
          )}
          {view && <IdentitySection view={view} />}

          {view?.kind === 'installed' && (
            <div className="mt-3 flex items-center gap-2 text-sm">
              <span className="text-text-secondary">{i18n._('sys.app_management.run.runtime_state')}</span>
              <span
                data-testid="runtime-state"
                className={view.appRunning ? 'font-medium text-green-700' : 'font-medium text-gray-600'}
              >
                {view.appRunning
                  ? i18n._('sys.app_management.run.running')
                  : i18n._('sys.app_management.run.idle')}
              </span>
            </div>
          )}
          <RunOutcomePanel />

          <Separator className="my-4" />

          <div className="space-y-2">
            <p className="text-sm font-medium">{i18n._('sys.app_management.install.title')}</p>
            <p className="text-xs text-text-secondary">{i18n._('sys.app_management.install.note')}</p>
            <div className="flex flex-wrap items-center gap-2">
              <input
                id="neapp-file-input"
                type="file"
                accept=".neapp"
                data-testid="neapp-input"
                className="text-sm"
                onChange={(e: Event) => {
                  const target = e.target as HTMLInputElement
                  setSelectedFile(target.files && target.files.length > 0 ? target.files[0] : null)
                }}
              />
              <Button size="sm" onClick={handleInstall} disabled={!canInstall || !selectedFile}>
                {installing
                  ? i18n._('sys.app_management.install.submitting')
                  : i18n._('sys.app_management.install.button')}
              </Button>
            </div>
            {installing && (
              <p className="text-xs text-text-secondary" data-testid="install-progress">
                {i18n._('sys.app_management.install.progress')}
              </p>
            )}
            {installOutcome && !installing && (
              <p className="text-sm text-green-700" data-testid="install-outcome">
                {installOutcome.status === 'already_installed'
                  ? i18n._('sys.app_management.install.already_installed')
                  : i18n._('sys.app_management.install.installed')}
              </p>
            )}
          </div>

          <Separator className="my-4" />

          <div className="space-y-2">
            <p className="text-sm font-medium">{i18n._('sys.app_management.run.title')}</p>
            <p className="text-xs text-text-secondary">{i18n._('sys.app_management.run.note')}</p>
            <Button size="sm" onClick={runOnce} disabled={!canRun} data-testid="run-button">
              {running
                ? i18n._('sys.app_management.run.submitting')
                : i18n._('sys.app_management.run.button')}
            </Button>
            {view?.kind === 'installed' && view.appRunning && (
              <p className="text-xs text-text-secondary">
                {i18n._('sys.app_management.run.busy_note')}
              </p>
            )}
          </div>

          <Separator className="my-4" />

          <div className="space-y-2">
            <p className="text-sm font-medium">{i18n._('sys.app_management.uninstall.title')}</p>
            <p className="text-xs text-text-secondary">{i18n._('sys.app_management.uninstall.note')}</p>
            <Button
              variant="destructive"
              size="sm"
              onClick={() => setConfirmOpen(true)}
              disabled={!canUninstall}
              data-testid="uninstall-button"
            >
              {uninstalling
                ? i18n._('sys.app_management.uninstall.submitting')
                : i18n._('sys.app_management.uninstall.button')}
            </Button>
          </div>
        </CardContent>
      </Card>

      <Dialog open={confirmOpen} onOpenChange={setConfirmOpen}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>{i18n._('sys.app_management.uninstall.confirm_title')}</DialogTitle>
            <DialogDescription>
              {i18n._('sys.app_management.uninstall.confirm_body')}
            </DialogDescription>
          </DialogHeader>
          <DialogFooter>
            <Button variant="outline" size="sm" onClick={() => setConfirmOpen(false)}>
              {i18n._('sys.app_management.uninstall.cancel')}
            </Button>
            <Button variant="destructive" size="sm" onClick={handleUninstall} data-testid="confirm-uninstall">
              {i18n._('sys.app_management.uninstall.confirm')}
            </Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>
    </div>
  )
}
