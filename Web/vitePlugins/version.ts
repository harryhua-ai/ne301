import { readFileSync } from 'fs';
import path from 'path';
import type { Plugin } from 'vite';

export default function version(): Plugin {
  return {
    name: 'version-log',
    transformIndexHtml(html: string) {
      // Read package.json to get version number
      const packageJsonPath = path.resolve(process.cwd(), 'package.json');
      const packageJson = JSON.parse(readFileSync(packageJsonPath, 'utf-8'));
      const version = packageJson.version;

      // Inject version output script before </body> tag
      const versionScript = `
    <script>
      (function() {
        var v = '${version}';
        if (typeof console !== 'undefined' && console.log) {
          console.log(
            '%c🚀 %cVersion:%c ' + v + ' %c',
            'font-size: 16px;',
            'color: #666; font-weight: bold; font-size: 14px; padding: 4px 8px; background: #f0f0f0; border-radius: 4px 0 0 4px;',
            'color: #4CAF50; font-weight: bold; font-size: 14px; padding: 4px 8px; background: #f0f0f0; border-radius: 0 4px 4px 0;',
            ''
          );
        }
      })();
    </script>`;
      
      return html.replace('</body>', `${versionScript}\n  </body>`);
    },
  };
}
