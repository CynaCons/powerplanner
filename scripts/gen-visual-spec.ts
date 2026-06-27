/**
 * Visual specification generator.
 *
 * Renders each conformance fixture document with the real <GanttRenderer>
 * (the web reference implementation) and writes the SVG to spec/visual/.
 * The visual spec is therefore generated, not hand-drawn: it cannot drift
 * from what the web app actually produces.
 *
 * Run: npx vite-node scripts/gen-visual-spec.ts
 */
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { readFileSync, writeFileSync, mkdirSync, readdirSync } from 'node:fs';
import { resolve, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';
import { GanttRenderer } from '../src/embed';
import { layoutDocument } from '../src/layout/engine';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const fixturesDir = resolve(root, 'spec/fixtures');
const outDir = resolve(root, 'spec/visual');
mkdirSync(outDir, { recursive: true });

const WIDTH = 920;
const fixtures = readdirSync(fixturesDir).filter(
  (f) => f.endsWith('.json') && !f.endsWith('.expected.json'),
);

const figures: Array<{ figure: string; fixture: string; width: number; height: number }> = [];

for (const file of fixtures) {
  const fx = JSON.parse(readFileSync(resolve(fixturesDir, file), 'utf8'));
  if (!fx.document) continue;
  const doc = fx.document;
  const viewStart = fx.view?.viewStart ?? doc.tasks?.[0]?.start ?? '2026-01-01';
  const lay = layoutDocument({ doc, viewStart, pxPerDay: 1 });
  const height = Math.round(lay.chartHeight + 80);

  const svg = renderToStaticMarkup(
    createElement(GanttRenderer, { document: doc, width: WIDTH, height, options: { themeOverride: 'light', showToday: false } }),
  );

  const name = basename(file, '.json');
  writeFileSync(resolve(outDir, `${name}.svg`), `${svg}\n`, 'utf8');
  figures.push({ figure: `${name}.svg`, fixture: file, width: WIDTH, height });
  // eslint-disable-next-line no-console
  console.log(`wrote spec/visual/${name}.svg (${WIDTH}x${height})`);
}

writeFileSync(
  resolve(outDir, 'index.json'),
  `${JSON.stringify({ generatedFrom: 'src/embed/GanttRenderer', figures }, null, 2)}\n`,
  'utf8',
);
