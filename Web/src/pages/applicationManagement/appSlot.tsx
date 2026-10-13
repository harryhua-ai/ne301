import type { ComponentChildren } from 'preact'
import type { AppAuthBlock, AppRecord, AppView } from '@/store/appManagement'

export interface BusinessUiEntry {
  labelKey: string
  render: (record: AppRecord) => ComponentChildren
}

export const businessUiCatalog: Record<string, BusinessUiEntry> = {}

export function businessUiFor(appId: string): BusinessUiEntry | null {
  return businessUiCatalog[appId] || null
}

export const confirmedV1SingleRunAppIds: ReadonlySet<string> = new Set(['hello-app'])

export function runOnceEligible(appId: string): boolean {
  if (businessUiFor(appId) !== null) return false
  return confirmedV1SingleRunAppIds.has(appId)
}

export type FirstSlot =
  | { kind: 'deploy'; entry: null }
  | { kind: 'manage'; entry: null }
  | { kind: 'business'; entry: BusinessUiEntry }
  | { kind: 'status'; entry: null }

export function firstSlotFor(view: AppView | null, authBlock: AppAuthBlock): FirstSlot {
  if (authBlock !== null || view === null) {
    return { kind: 'status', entry: null }
  }
  if (view.kind === 'not_installed') {
    return { kind: 'deploy', entry: null }
  }
  if (view.kind === 'installed') {
    const entry = businessUiFor(view.record.appId)
    return entry ? { kind: 'business', entry } : { kind: 'manage', entry: null }
  }
  return { kind: 'status', entry: null }
}
