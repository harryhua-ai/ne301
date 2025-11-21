import { render } from 'react-dom';
import '@/styles/index.css';
import App from './app.tsx';
import { I18nWrapper } from './i18n';
import { LanguageProvider } from './hooks/useLanguageProvider';
// eslint-disable-next-line import/no-unresolved
// @ts-expect-error: render icons
import 'virtual:svg-icons-register'

render(
  <I18nWrapper>
    <LanguageProvider>
      <App />
    </LanguageProvider>
  </I18nWrapper>,
  document.getElementById('app')!
);