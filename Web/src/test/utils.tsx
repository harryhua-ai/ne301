import { render } from '@testing-library/preact';
import { BrowserRouter } from 'react-router-dom';
import { I18nWrapper } from '@/i18n';

// Custom render function with routing and i18n support
export function renderWithProviders(ui: React.ReactElement, options = {}) {
  return render(
    <I18nWrapper>
      <BrowserRouter>
        {ui}
      </BrowserRouter>
    </I18nWrapper>,
    options,
  );
}

// Re-export all testing utilities
export * from '@testing-library/preact';
