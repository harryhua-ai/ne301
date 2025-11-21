// Import JSON translation files
import zhSys from './zh/sys.json';
import enSys from './en/sys.json';
import zhErrors from './zh/errors.json';
import enErrors from './en/errors.json';
import zhCom from './zh/com.json';
import enCom from './en/com.json';

// Use nested object structure to maintain original nesting level
export const locales = {
  zh: { sys: zhSys.sys, common: zhCom.common, errors: zhErrors.errors },
  en: { sys: enSys.sys, common: enCom.common, errors: enErrors.errors }
};

// Export types
export type LocaleType = 'zh' | 'en';

// Export translation type - needs to be redefined as it's no longer flat structure
export type MessageKey = string;

// Export translation function type
export type TranslateFunction = (key: string) => string;

// Default language
export const DEFAULT_LOCALE: LocaleType = 'en';

// Supported language list
export const SUPPORTED_LOCALES: LocaleType[] = ['zh', 'en'];

// Language display names
export const LOCALE_NAMES: Record<LocaleType, string> = {
  zh: '简体中文',
  en: 'English',
};

// Export utility functions
export { t, createTranslator, getMessageKeys, hasMessage } from './utils';
