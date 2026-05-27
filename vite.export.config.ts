import { defineConfig, type Plugin } from 'vite';
import react from '@vitejs/plugin-react';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { readFileSync, renameSync, writeFileSync, existsSync, unlinkSync } from 'node:fs';
import { resolve } from 'node:path';

// Inline the favicon as a data URI so the output file is truly self-contained
function inlineFavicon(): Plugin {
  return {
    name: 'powerplanner-inline-favicon',
    apply: 'build',
    transformIndexHtml: {
      order: 'pre',
      handler(html) {
        const faviconPath = resolve('public/favicon.svg');
        if (!existsSync(faviconPath)) return html;
        const svg = readFileSync(faviconPath, 'utf8');
        const dataUri = `data:image/svg+xml;utf8,${encodeURIComponent(svg)}`;
        return html.replace(/href=["']\/favicon\.svg["']/g, `href="${dataUri}"`);
      },
    },
  };
}

// Rename the output to PowerPlanner.html and remove favicon asset (now inlined)
function renameOutput(): Plugin {
  return {
    name: 'powerplanner-rename-output',
    apply: 'build',
    closeBundle() {
      const dir = 'dist-template';
      const indexPath = resolve(dir, 'index.html');
      const namedPath = resolve(dir, 'PowerPlanner.html');
      const faviconPath = resolve(dir, 'favicon.svg');
      if (existsSync(indexPath)) {
        renameSync(indexPath, namedPath);
        // also keep a copy as index.html so `npx serve` works
        const content = readFileSync(namedPath, 'utf8');
        writeFileSync(indexPath, content);
      }
      if (existsSync(faviconPath)) unlinkSync(faviconPath);
    },
  };
}

export default defineConfig({
  plugins: [react(), viteSingleFile(), inlineFavicon(), renameOutput()],
  build: {
    outDir: 'dist-template',
    assetsInlineLimit: 100_000_000,
    cssCodeSplit: false,
    emptyOutDir: true,
    rollupOptions: {
      output: {
        inlineDynamicImports: true,
        manualChunks: undefined,
      },
    },
  },
});
