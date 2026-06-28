// Record a short clip of the real web app via Playwright (for a GIF/anim demo).
//   node scripts/record-web.mjs <url> <outDir>
import { chromium } from 'playwright';

const url = process.argv[2] || 'http://localhost:5180';
const outDir = process.argv[3] || '.';

const browser = await chromium.launch();
const context = await browser.newContext({
  viewport: { width: 1280, height: 720 },
  recordVideo: { dir: outDir, size: { width: 1280, height: 720 } },
});
const page = await context.newPage();
await page.goto(url, { waitUntil: 'networkidle' });
await page.waitForTimeout(1400);

// Cycle the time scales to show calendar-true rescaling (Shift+D/W/M/Q/Y).
for (const k of ['Shift+KeyW', 'Shift+KeyM', 'Shift+KeyQ', 'Shift+KeyY', 'Shift+KeyM', 'Shift+KeyW']) {
  await page.keyboard.press(k);
  await page.waitForTimeout(950);
}
await page.keyboard.press('Home'); // fit to data
await page.waitForTimeout(900);

await page.close();
const videoPath = await page.video().path();
await context.close();
await browser.close();
console.log(videoPath);
