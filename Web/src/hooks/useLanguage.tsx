import { useContext } from 'preact/hooks';
import { LanguageContext } from './useLanguageProvider';

export function useLanguage() {
  const context = useContext(LanguageContext);
  if (context === undefined) {
    throw new Error('useLanguage must be used within a LanguageProvider');
  }
  return context;
}
