import { useLingui } from '@lingui/react';

export default function NotFound() {
  const { i18n } = useLingui();

  return (
    <div className="flex items-center justify-center min-h-screen">
      <div className="text-center">
        <h1 className="text-5xl font-bold text-gray-900 mb-4">
          {i18n._('common.not_found_title')}
        </h1>
        <p className="text-gray-600 mb-4 text-base">
          {i18n._('common.not_found_message')}
        </p>
        <a href="/" className="text-primary hover:text-[#f24d00] underline">
          {i18n._('common.not_found_back_home')}
        </a>
      </div>
    </div>
  );
} 