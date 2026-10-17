import { useEffect, useRef, useState } from 'preact/hooks'
import { useLingui } from '@lingui/react'
import { Card, CardContent } from '@/components/ui/card'
import { Tabs, TabsList, TabsTrigger, TabsContent } from '@/components/ui/tabs'
import MqttModule from './mqtt-module'
import WebhookModule from './webhook-module'
import AppDialog, { type AppDialogState } from './appDialog'
import SidecarAppFrame from './sidecarAppFrame'
import { businessUiFor, firstSlotFor } from './appSlot'
import { useAppManagementStore } from '@/store/appManagement'

const SLOT_BUTTON_BASE =  'h-[calc(100%-1px)] flex-1 inline-flex items-center justify-center gap-1 rounded-md border px-2 py-1 text-sm font-medium whitespace-nowrap transition-colors'

export default function ApplicationManagement() {
  const { i18n } = useLingui()
  const view = useAppManagementStore((s) => s.view)
  const authBlock = useAppManagementStore((s) => s.authBlock)
  const installOutcome = useAppManagementStore((s) => s.installOutcome)
  const sidecar = useAppManagementStore((s) => s.sidecar)
  const refresh = useAppManagementStore((s) => s.refresh)
  const refreshSidecar = useAppManagementStore((s) => s.refreshSidecar)
  const [dialogState, setDialogState] = useState<AppDialogState | null>(null)
  const [activeTab, setActiveTab] = useState('mqtt-module')
  const userTouchedTab = useRef(false)

  useEffect(() => {
    refresh()
    refreshSidecar()
  }, [refresh, refreshSidecar])

  const slot = firstSlotFor(view, authBlock, sidecar)

  useEffect(() => {
    if (slot.kind === 'sidecar' && !userTouchedTab.current) {
      setActiveTab('business')
    }
    if (slot.kind !== 'sidecar' && slot.kind !== 'business' && activeTab === 'business') {
      setActiveTab('mqtt-module')
    }
  }, [slot.kind, activeTab])

  const renderSlot = () => {
    if (slot.kind === 'sidecar') {
      return (
        <TabsTrigger value="business" data-testid="slot-business">
          {i18n._('sys.app_management.sidecar.tab_label')}
        </TabsTrigger>
      )
    }
    if (slot.kind === 'business') {
      return (
        <TabsTrigger value="business" data-testid="slot-business">
          {i18n._(slot.entry.labelKey)}
        </TabsTrigger>
      )
    }
    if (slot.kind === 'deploy') {
      return (
        <button
          type="button"
          data-testid="slot-deploy"
          onClick={() => setDialogState('deploy')}
          className={`${SLOT_BUTTON_BASE} border-dashed border-[#b6c4d1] bg-background text-foreground hover:bg-[#fbfcfe]`}
        >
          ＋ {i18n._('sys.app_management.slot.deploy')}
        </button>
      )
    }
    if (slot.kind === 'manage') {
      return (
        <button
          type="button"
          data-testid="slot-manage"
          onClick={() => setDialogState('manage')}
          className={`${SLOT_BUTTON_BASE} border-dashed border-[#b6c4d1] bg-background text-foreground hover:bg-[#fbfcfe]`}
        >
          ⚙ {i18n._('sys.app_management.slot.manage')}
        </button>
      )
    }
    return (
      <button
        type="button"
        data-testid="slot-status"
        onClick={() => setDialogState('status')}
        className={`${SLOT_BUTTON_BASE} border-[#e7c3a1] bg-[#fff1e8] text-[#b9672d] hover:bg-[#ffe9da]`}
      >
        ⚠ {i18n._('sys.app_management.slot.status')}
      </button>
    )
  }

  const renderNotice = () => {
    if (authBlock !== null) {
      return (
        <div className="mb-3 rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800">
          {authBlock === 'unauthorized'
            ? i18n._('sys.app_management.auth.unauthorized')
            : i18n._('sys.app_management.auth.forbidden')}
          {authBlock === 'unauthorized' && (
            <a className="ml-2 underline" href="/login" data-testid="relogin-link">
              {i18n._('sys.app_management.auth.relogin')}
            </a>
          )}
        </div>
      )
    }
    if (view?.kind === 'undetermined') {
      return (
        <div
          className="mb-3 rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700"
          data-testid="undetermined-warning"
        >
          <p className="font-medium">{i18n._('sys.app_management.page.abnormal_heading')}</p>
          <p className="mt-1">{i18n._('sys.app_management.page.abnormal_detail')}</p>
          {view.reason && (
            <p className="mt-1 font-mono text-xs">
              {i18n._('sys.app_management.page.abnormal_reason')}: {view.reason}
            </p>
          )}
        </div>
      )
    }
    if (view?.kind === 'unavailable') {
      return (
        <div
          className="mb-3 rounded border border-orange-300 bg-orange-50 p-3 text-sm text-orange-800"
          data-testid="unavailable-warning"
        >
          <p>
            {i18n._('sys.app_management.state.unavailable_note')}{' '}
            <span className="font-mono text-xs">{view.reason}</span>
          </p>
        </div>
      )
    }
    if (view?.kind === 'cleanup_pending') {
      return (
        <div
          className="mb-3 rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700"
          data-testid="cleanup-pending-warning"
        >
          <p className="font-medium">{i18n._('sys.app_management.cleanup.title')}</p>
          <p className="mt-1">{i18n._('sys.app_management.cleanup.detail')}</p>
        </div>
      )
    }
    if (view?.kind === 'installed' && !businessUiFor(view.record.appId) && slot.kind !== 'sidecar') {
      return (
        <div
          className="mb-3 rounded border border-[#e6eaee] bg-[#f7f9fb] p-3 text-sm"
          data-testid="no-business-notice"
        >
          <p className="text-[#334354]">{i18n._('sys.app_management.page.installed_no_business')}</p>
          <p className="mt-1 text-xs text-[#718093]">
            {i18n._('sys.app_management.page.installed_no_business_hint')}
          </p>
        </div>
      )
    }
    return null
  }

  const renderInstallOutcome = () => {
    if (!installOutcome || view?.kind !== 'installed') return null
    return (
      <p className="mb-3 text-sm text-green-700" data-testid="install-outcome">
        {installOutcome.status === 'already_installed'
          ? i18n._('sys.app_management.install.already_installed')
          : i18n._('sys.app_management.install.installed')}
      </p>
    )
  }

  return (
    <div className="flex justify-center">
      <Card className="sm:w-xl w-full mx-4 my-4">
        <CardContent>
          <Tabs
            value={activeTab}
            onValueChange={(value: string) => {
              userTouchedTab.current = true
              setActiveTab(value)
            }}
          >
            <TabsList className="w-full">
              {renderSlot()}
              <TabsTrigger value="mqtt-module">MQTT/MQTTS</TabsTrigger>
              <TabsTrigger value="webhook-module">Webhook</TabsTrigger>
            </TabsList>
            {activeTab !== 'business' && (
              <div>
                {renderNotice()}
                {renderInstallOutcome()}
              </div>
            )}
            <TabsContent value="mqtt-module">
              <MqttModule />
            </TabsContent>
            <TabsContent value="webhook-module">
              <WebhookModule />
            </TabsContent>
            {(slot.kind === 'business' || slot.kind === 'sidecar') && (
              <TabsContent value="business">
                <div className="mb-3 flex justify-end">
                  <button
                    type="button"
                    data-testid="business-manage"
                    onClick={() => setDialogState('manage')}
                    className="inline-flex items-center gap-1 text-sm text-text-secondary transition-colors hover:text-primary"
                  >
                    ⚙ {i18n._('sys.app_management.slot.manage')}
                  </button>
                </div>
                {slot.kind === 'sidecar' ? (
                  view?.kind === 'installed' && (
                    <SidecarAppFrame
                      projection={slot.projection}
                      appId={view.record.appId}
                    />
                  )
                ) : (
                  view?.kind === 'installed' && slot.entry.render(view.record)
                )}
              </TabsContent>
            )}
          </Tabs>
        </CardContent>
      </Card>
      <AppDialog
        state={dialogState}
        onStateChange={setDialogState}
        onClose={() => setDialogState(null)}
        onBusinessActivated={() => {
          setDialogState(null)
          setActiveTab('business')
        }}
      />
    </div>
  )
}
