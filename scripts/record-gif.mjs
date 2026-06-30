// Record a looping GIF of the web app via Playwright frames -> gifenc.
//   node scripts/record-gif.mjs <url> <out.gif>
import { chromium } from 'playwright';
import { PNG } from 'pngjs';
import gifenc from 'gifenc';
const { GIFEncoder, quantize, applyPalette } = gifenc;
import { writeFileSync } from 'node:fs';

const url = process.argv[2] || 'http://localhost:5180';
const out = process.argv[3] || 'web-demo.gif';
const W = 1000, H = 560;

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: W, height: H } });
await page.goto(url, { waitUntil: 'networkidle' });
await page.waitForTimeout(1000);

const frames = [];
async function snap() { frames.push(PNG.sync.read(await page.screenshot({ type: 'png' }))); }

await snap();
for (const k of ['Shift+KeyW', 'Shift+KeyM', 'Shift+KeyQ', 'Shift+KeyY', 'Shift+KeyM', 'Shift+KeyW']) {
  await page.keyboard.press(k);
  await page.waitForTimeout(300);
  await snap();
}
await page.keyboard.press('Home');
await page.waitForTimeout(300);
await snap();
await browser.close();

const gif = GIFEncoder();
frames.forEach((f, i) => {
  const data = new Uint8Array(f.data);
  const palette = quantize(data, 256);
  const index = applyPalette(data, palette);
  gif.writeFrame(index, f.width, f.height, { palette, delay: 650, repeat: 0 });
});
gif.finish();
writeFileSync(out, Buffer.from(gif.bytes()));
console.log(`wrote ${out} (${frames.length} frames, ${W}x${H})`);
