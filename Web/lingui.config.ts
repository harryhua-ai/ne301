import type { LinguiConfig } from '@lingui/conf';

const config: LinguiConfig = {
  locales: ['zh', 'en'],
  sourceLocale: 'zh',
  catalogs: [
    {
      path: '<rootDir>/src/locales/{locale}/messages',
      include: ['src'],
    },
  ],
  format: 'po',
};

export default config;
