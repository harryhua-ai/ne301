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

export const confirmedV1SingleRunPackageDigests: ReadonlySet<string> = new Set([
  'f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8',
  '2a86a2982c042385850daac8962b3cef1d49f838e03a503032b0c6a5874a644f',
  'd803bfe64a59f39b3472ef6af1c29e85fa98a6634246460db3466e38fa641a02',
])

export function runOnceEligible(appId: string, packageSha256: string): boolean {
  if (businessUiFor(appId) !== null) return false
  return confirmedV1SingleRunPackageDigests.has(packageSha256)
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
