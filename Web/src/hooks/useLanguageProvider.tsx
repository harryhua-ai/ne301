import { createContext, useContext, useState, useEffect, useMemo, type ReactNode } from 'preact/compat';
import { getItem, setItem } from '@/utils/storage';
import { setLocale as setI18nLocale } from '@/i18n';

type LocaleType = 'zh' | 'en';

interface LanguageContextType {
  locale: LocaleType;
  setLocale: (locale: LocaleType) => void;
}

const LanguageContext = createContext<LanguageContextType | undefined>(undefined);

export { LanguageContext };

export function LanguageProvider({ children }: { children: ReactNode }) {
  const [locale, setLocaleState] = useState<LocaleType>('en');

  useEffect(() => {
    const savedLocale = getItem<LocaleType>('locale');
    if (savedLocale) {
      setLocaleState(savedLocale);
      // Also set i18n during initialization
      setI18nLocale(savedLocale);
    }
  }, []);

  const setLocale = (newLocale: LocaleType) => {
    setLocaleState(newLocale);
    setItem('locale', newLocale);
    // Update i18n when switching language
    setI18nLocale(newLocale);
  };
  const contextValue = useMemo(() => ({ locale, setLocale }), [locale]);

  return (
    <LanguageContext.Provider value={contextValue}>
      {children}
    </LanguageContext.Provider>
  );
}

export function useLanguage() {
  const context = useContext(LanguageContext);
  if (context === undefined) {
    throw new Error('useLanguage must be used within a LanguageProvider');
  }
  return context;
}