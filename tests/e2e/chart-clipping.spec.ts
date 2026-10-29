import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';
import { PNG } from 'pngjs';

/**
 * SRS-TAX-040 — the clip window is the plot area, and it does not move.
 *
 * An SVG `clip-path` resolves in the referencing element's own post-transform
 * user space. The body group carries `translate(gutter, axisH - scrollY)`, so a
 * clip rect written `y=0` on that group has its top edge at screen
 * `axisH - scrollY`: the window slides up with the scroll. Three things follow
 * from that one line, and this spec asserts all three against what a person
 * sees rather than against the code that produces it.
 *
 * Observed red on afe451b (before the fix):
 *   header   bars hit-testable inside the 56px calendar band at scroll 300/900
 *   collapse bars down the plot centre went 15 -> 10 -> 0 as the scroll grew
 *   pill     zero today-coloured pixels in the band — the pill is never drawn
 */

/** The chart is the product's full-size surface; a phone has no plot to clip. */
const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/** The axis band's height, read from the rule the chart draws under it. */
async function axisHeight(page: Page) {
  return page.evaluate(() => {
    const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
    const line = Array.from(svg.querySelectorAll(':scope > line')).find(
      (l) =>
        (l as SVGLineElement).y1.baseVal.value === (l as SVGLineElement).y2.baseVal.value &&
        (l as SVGLineElement).x1.baseVal.value > 0,
    ) as SVGLineElement;
    return line.y1.baseVal.value;
  });
}

async function scrollBy(page: Page, dy: number) {
  const box = (await page.locator('.chart-area > svg').first().boundingBox())!;
  await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.6);
  await page.mouse.wheel(0, dy);
  await page.waitForTimeout(60);
}

/** Every chart element hit-testable inside the calendar header band. */
async function bodyInsideHeader(page: Page, axisH: number) {
  return page.evaluate((axis) => {
    const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
    const r = svg.getBoundingClientRect();
    const gutter = 200;
    const found: string[] = [];
    for (let x = gutter + 10; x < r.width - 8; x += 11) {
      for (let y = 3; y < axis - 3; y += 5) {
        const el = document.elementFromPoint(r.left + x, r.top + y);
        const owner = el?.closest('[data-pp-kind]');
        const kind = owner?.getAttribute('data-pp-kind');
        if (kind === 'task' || kind === 'milestone' || kind === 'bracket') {
          found.push(`${kind}@${Math.round(x)},${y}`);
        }
      }
    }
    return found;
  }, axisH);
}

/** Distinct task bars hit-testable anywhere in the plot area. */
async function barsInPlot(page: Page, axisH: number) {
  return page.evaluate((axis) => {
    const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
    const r = svg.getBoundingClientRect();
    const seen = new Set<string>();
    for (let y = axis + 4; y < r.height - 4; y += 4) {
      for (let x = 210; x < r.width - 8; x += 13) {
        const el = document.elementFromPoint(r.left + x, r.top + y);
        const bar = el?.closest('[data-pp-kind="task"]');
        if (bar) seen.add(bar.getAttribute('data-pp-id')!);
      }
    }
    return seen.size;
  }, axisH);
}

/** Pixels of the TODAY accent inside the header band. Clipping is a paint
 *  fact, and paint is the only honest way to read it. */
async function todayPixelsInHeader(page: Page, axisH: number) {
  const box = (await page.locator('.chart-area > svg').first().boundingBox())!;
  const buf = await page.screenshot({
    clip: { x: box.x, y: box.y, width: box.width, height: axisH },
  });
  const png = PNG.sync.read(buf);
  const scale = png.width / box.width;
  let hits = 0;
  for (let i = 0; i < png.data.length; i += 4) {
    const [r, g, b] = [png.data[i], png.data[i + 1], png.data[i + 2]];
    // --color-today, light theme: #B3382C.
    if (Math.abs(r - 0xb3) < 24 && Math.abs(g - 0x38) < 24 && Math.abs(b - 0x2c) < 24) hits++;
  }
  return hits / (scale * scale);
}

test.describe('chart clipping (SRS-TAX-040)', () => {
  test.skip(DESKTOP_ONLY, 'the plot area needs a desktop window');

  test('no chart element paints over the calendar header, at any scroll offset', async ({ page }) => {
    await ready(page);
    const axisH = await axisHeight(page);

    expect(await bodyInsideHeader(page, axisH)).toEqual([]);

    await scrollBy(page, 300);
    expect(await bodyInsideHeader(page, axisH)).toEqual([]);

    await scrollBy(page, 600);
    expect(await bodyInsideHeader(page, axisH)).toEqual([]);
  });

  test('the foot of the plan is not blanked as the chart scrolls', async ({ page }) => {
    await ready(page);
    const axisH = await axisHeight(page);

    const atTop = await barsInPlot(page, axisH);
    expect(atTop).toBeGreaterThan(5);

    await scrollBy(page, 600);
    expect(await barsInPlot(page, axisH)).toBeGreaterThan(5);

    // All the way down: the last rows are still a chart, not bare separators.
    await scrollBy(page, 4000);
    expect(await barsInPlot(page, axisH)).toBeGreaterThan(0);
  });

  /**
   * Rewritten for SRS-TAX-053. The pill used to be REQUIRED in the band — the
   * clipping bug meant it was never painted at all, so its pixels there were
   * the proof the clip had been fixed. The owner then reported the cost of
   * that position on 2026-10-29: the pill buries whichever calendar label it
   * lands on, and in his capture it ate "Nov". The pill now rides the top of
   * the plot and only a hairline crosses the bands. The clip fact this test
   * exists for is unchanged — the marker must still be PAINTED — so it is now
   * read as a rule in the band and a pill below it.
   */
  test('the today marker crosses the band as a rule, with its pill below', async ({ page }) => {
    await ready(page);
    const axisH = await axisHeight(page);
    // A 1px rule down a 44-64px band, not a 46x16 pill: present, and thin.
    const inBand = await todayPixelsInHeader(page, axisH);
    expect(inBand).toBeGreaterThan(10);
    expect(inBand).toBeLessThan(200);

    const below = await page.evaluate((h) => {
      const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
      const pill = svg.querySelector('.axis-today-pill');
      if (!pill) return null;
      return pill.getBoundingClientRect().top - svg.getBoundingClientRect().top - h;
    }, axisH);
    expect(below).not.toBeNull();
    expect(below!).toBeGreaterThanOrEqual(0);
  });
});
