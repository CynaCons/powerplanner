import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { existsSync, readFileSync } from 'node:fs';
import { homedir } from 'node:os';
import { resolve } from 'node:path';

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

export default defineConfig(({ command, mode }) => {
  const addinDev = mode === 'addin';
  const https = command === 'serve' && addinDev ? getOfficeDevHttpsOptions() : undefined;

  return {
    plugins: [react()],
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