import { i18n } from '@lingui/core';
import { I18nProvider } from '@lingui/react';
import { locales, DEFAULT_LOCALE, type LocaleType } from './locales';

// Flatten nested objects
function flattenMessages(obj: any, prefix = ''): Record<string, string> {
  const result: Record<string, string> = {};
  
  Object.keys(obj).forEach(key => {
    const value = obj[key];
    const fullKey = prefix ? `${prefix}.${key}` : key;
    
    if (typeof value === 'object' && value !== null) {
      // Recursively process nested objects
      Object.assign(result, flattenMessages(value, fullKey));
    } else {
      // Leaf node, add directly
      result[fullKey] = value;
    }
  });
  
  return result;
}

// Get flattened messages
function getFlattenedMessages(locale: LocaleType) {
  return flattenMessages(locales[locale]);
}

// Initialize i18n
i18n.loadAndActivate({
  locale: DEFAULT_LOCALE,
  messages: getFlattenedMessages(DEFAULT_LOCALE),
});

export function setLocale(locale: LocaleType) {
  i18n.loadAndActivate({ 
    locale, 
    messages: getFlattenedMessages(locale) 
  });
}

// Create I18nProvider component
export function I18nWrapper({ children }: { children: React.ReactNode }) {
  return <I18nProvider i18n={i18n}>{children}</I18nProvider>;
}

// Re-export locales related types and constants
export {
  locales,
  DEFAULT_LOCALE,
  SUPPORTED_LOCALES,
  LOCALE_NAMES,
} from './locales';
export type { LocaleType, MessageKey, TranslateFunction } from './locales';

export default i18n;
