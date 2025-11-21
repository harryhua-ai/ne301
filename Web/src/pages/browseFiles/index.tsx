import { useLingui } from '@lingui/react';

export default function BrowseFiles() {
  const { i18n } = useLingui();

  return (
    <div className="p-6">
      <h2 className="text-2xl font-bold text-gray-900 mb-4">
        {i18n._('sys.menu.file_browsing')}
      </h2>
      <p className="text-gray-600">Browse files page content</p>
    </div>
  );
} 