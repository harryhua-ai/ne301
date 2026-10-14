import english from '@/locales/en/sys.json'

const lookup = (value: unknown) => {
  const parts = String(value).split('.')
  const root = parts.shift() || ''
  let result: unknown = (english as Record<string, unknown>)[root]
  for (const part of parts) {
    result = result && typeof result === 'object' ? (result as Record<string, unknown>)[part] : undefined
  }
  return typeof result === 'string' ? result : String(value)
}

export const useLingui = () => ({ i18n: { _: lookup } })
