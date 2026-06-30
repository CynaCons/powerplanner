// Screenshot a URL to a PNG.  node scripts/shot.mjs <url> <out.png> [w] [h]
import { chromium } from 'playwright';
const [url, out, w = '1280', h = '720'] = process.argv.slice(2);
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: +w, height: +h }, deviceScaleFactor: 2 });
await page.goto(url, { waitUntil: 'networkidle' });
await page.waitForTimeout(1200);
await page.screenshot({ path: out });
await browser.close();
console.log('wrote', out);
