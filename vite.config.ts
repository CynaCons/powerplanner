import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';
import { existsSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { resolve } from 'node:path';
import { agentTokenPlugin } from './vite-plugin-agent-token';

function getOfficeDevHttpsOptions() {
  const certDir = resolve(homedir(), '.office-addin-dev-certs');
  const certPath = resolve(certDir, 'localhost.crt');
  const keyPath = resolve(certDir, 'localhost.key');
  const caPath = resolve(certDir, 'ca.crt');

  if (!existsSync(certPath) || !existsSync(keyPath)) {
    throw new Error(
      `Missing Office localhost development certificate in ${certDir}. Run "npm run addin:certs" once, then use "npm run dev:addin".`,
    );
  }

  return {
    cert: readFileSync(certPath),
    key: readFileSync(keyPath),
    ca: existsSync(caPath) ? readFileSync(caPath) : undefined,
  };
}

/**
 * Hand the dev server's Save the real built app (SRS-PERS-034).
 *
 * Under `npm run dev` the page is a stub whose only script is
 * `/src/main.tsx`, so saving a clone of it produces a file that opens to
 * nothing. The save path fetches the built single-file app instead — but
 * fetching `/dist-template/index.html` does NOT return that file: Vite owns
 * every .html under the root and answers with its own transform of it, dev
 * client and all. Observed: a 37 KB save containing
 * `src="/dist-template/index.html?html-proxy"`.
 *
 * So it is served here, read from disk, as text/plain, under a name Vite has
 * no opinion about.
 */
function builtTemplatePlugin(): Plugin {
  return {
    name: 'powerplanner-built-template',
    apply: 'serve',
    configureServer(server) {
      server.middlewares.use('/__built-template', (_req, res) => {
        const path = resolve(__dirname, 'dist-template/index.html');
        if (!existsSync(path)) {
          res.statusCode = 404;
          res.end('no built template — run npm run build:template');
          return;
        }
        res.setHeader('Content-Type', 'text/plain; charset=utf-8');
        res.setHeader('Cache-Control', 'no-store');
        res.end(readFileSync(path, 'utf8'));
      });
    },
  };
}

export default defineConfig(({ command, mode }) => {
  const addinDev = mode === 'addin';
  const https = command === 'serve' && addinDev ? getOfficeDevHttpsOptions() : undefined;

  return {
    plugins: [react(), agentTokenPlugin(), builtTemplatePlugin()],
    server: {
      host: 'localhost',
      port: 5180,
      strictPort: addinDev,
      https,
    },
    build: {
      rollupOptions: {
        input: {
          main: resolve(__dirname, 'index.html'),
          taskpane: resolve(__dirname, 'taskpane.html'),
        },
      },
    },
    optimizeDeps: {
      // Only force re-scan for add-in dev (https + multi-entry is more sensitive to stale pre-bundles).
      force: addinDev,
    },
  };
});