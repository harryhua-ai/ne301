import { defineConfig, loadEnv } from 'vite';
import preact from '@preact/preset-vite';
import tailwindcss from '@tailwindcss/vite';
import { lingui } from '@lingui/vite-plugin';
import path from 'path';
import createPlugins from './vitePlugins';
import { readFileSync } from 'fs';

// https://vite.dev/config/
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '')
  const packageJson = JSON.parse(readFileSync(path.resolve(__dirname, 'package.json'), 'utf-8'));
  return {
    base: env.VITE_APP_BASE || '/',
    define: {
      __APP_VERSION__: JSON.stringify(packageJson.version),
    },
    plugins: [
      preact(),
      tailwindcss(),
      lingui(),
      ...createPlugins
    ],
    server: {
      host: '0.0.0.0', // Listen on all network interfaces
      port: 5173,
      proxy: {
        '/api': {
          target: 'http://192.168.10.10',
          changeOrigin: true
        },
      },
    },
    resolve: {
      alias: {
        '@': path.resolve(__dirname, './src'),
        'react': path.resolve(__dirname, 'node_modules/preact/compat'),
        'react-dom': path.resolve(__dirname, 'node_modules/preact/compat'),
        "react-dom/test-utils": "preact/test-utils",
      },
    },
    esbuild: {
      jsx: 'automatic',
    },
    optimizeDeps: {
      include: ['@lingui/macro', 'zustand'],
    },
  };
});
