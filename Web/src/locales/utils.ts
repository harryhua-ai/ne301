import { locales, type LocaleType, type MessageKey } from './index.js';
import { getItem } from '@/utils/storage';
/**
 * Translation function
 * @param key Translation key, supports dot-separated nested access, e.g., 'header.header_device_model'
 * @param locale Language
 * @returns Translated text
 */
export function t(key: MessageKey): string {
  const locale = getItem('language') || 'zh';
  const messages = locales[locale as LocaleType];
  // Use dot-separated keys to access nested objects
  const keys = key.split('.');
  let result: any = messages;
  
  for (const k of keys) {
    if (result && typeof result === 'object' && k in result) {
      result = result[k];
    } else {
      console.warn(`Translation key "${key}" not found for locale "${locale}"`);
      return key;
    }
  }
  
  if (typeof result === 'string') {
    return result;
  } 
    console.warn(`Translation key "${key}" is not a string for locale "${locale}"`);
    return key;
}

/**
 * Create translation hook
 * @param locale Current language
 * @returns Translation function
 */
export function createTranslator(_locale: LocaleType) {
  return (key: MessageKey) => t(key);
}

/**
 * Get all translation keys
 * @returns Translation key array
 */
export function getMessageKeys(): MessageKey[] {
  // Since we're now using nested structure, this function needs to be reimplemented
  // Temporarily return empty array, or implement a recursive function to get all keys
  return [];
}

/**
 * Check if translation key exists
 * @param key Translation key
 * @returns Whether it exists
 */
export function hasMessage(key: string): key is MessageKey {
  const locale = getItem('language') || 'zh';
  const messages = locales[locale as LocaleType];
  const keys = key.split('.');
  let result: any = messages;
  
  for (const k of keys) {
    if (result && typeof result === 'object' && k in result) {
      result = result[k];
    } else {
      return false;
    }
  }
  
  return typeof result === 'string';
}
