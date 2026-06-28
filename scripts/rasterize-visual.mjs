// Rasterize spec/visual/*.svg -> PNG via Playwright/Chromium, so the web
// reference figures can be compared 1:1 with the native PNG exports.
//
//   node scripts/rasterize-visual.mjs
import { chromium } from 'playwright';
import { readFileSync, readdirSync } from 'node:fs';
import { resolve } from 'node:path';

const dir = resolve('spec/visual');
const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 2 });

for (const f of readdirSync(dir).filter((f) => f.endsWith('.svg'))) {
  const svg = readFileSync(resolve(dir, f), 'utf8');
  const html = `<!doctype html><meta charset="utf8"><body style="margin:0;background:#ffffff">${svg}</body>`;
  await page.setContent(html, { waitUntil: 'networkidle' });
  const el = await page.$('svg');
  const box = await el.boundingBox();
  await page.setViewportSize({ width: Math.ceil(box.width), height: Math.ceil(box.height) });
  const out = resolve(dir, f.replace('.svg', '.png'));
  await el.screenshot({ path: out });
  console.log('wrote', out);
}

await browser.close();
