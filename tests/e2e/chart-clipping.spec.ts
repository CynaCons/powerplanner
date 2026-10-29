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

/** Half-width of the column window sampled around the rule, in CSS pixels. */
const HALF_WINDOW_PX = 3;

/** Today-red INK at the rule's own x inside the header band, in whole-pixel
 *  equivalents, and how many of the band's scanlines carry any of it.
 *
 *  Clipping is a paint fact and paint is still the only honest way to read it,
 *  but this needs two corrections from the version it replaces.
 *
 *  It is read as COVERAGE, not as a colour match. The old probe counted pixels
 *  within 24 per channel of the literal token — an instrument built for the
 *  46x16 solid pill this test used to measure. A 1px stroke whose centre is
 *  not on a pixel centre spreads its one pixel of ink over two columns and
 *  neither reaches the ~88% coverage that threshold needs; `todayX` is
 *  `diffDays * pxPerDay` and the on-load fit makes `pxPerDay` fractional, so
 *  that is the ordinary case and the count came back 0 against a rule that was
 *  drawn perfectly correctly.
 *
 *  And it is read AT THE RULE, whose x comes from the DOM, rather than swept
 *  across the whole band. Measured on this band, a red-minus-green sweep finds
 *  warm pixels in 470 of 1440 columns: the calendar's own month and quarter
 *  labels are antialiased per subpixel, which splits the channels at every
 *  glyph edge. No colour test separates those from a red rule reliably — but
 *  the requirement was never about colour anywhere in the band. It is that the
 *  marker crosses THIS band at THIS x, as a rule and not as a pill, so that is
 *  what is measured. */
async function todayInkInHeader(page: Page, axisH: number) {
  const box = (await page.locator('.chart-area > svg').first().boundingBox())!;
  const probe = await page.evaluate(() => {
    const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
    const rule = svg.querySelector('.axis-today-rule');
    const raw = getComputedStyle(document.documentElement).getPropertyValue('--color-today').trim();
    const m = /^#([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec(raw);
    const warmth = m ? parseInt(m[1], 16) - parseInt(m[2], 16) : 0;
    if (!rule) return { warmth, x: null as number | null };
    const r = rule.getBoundingClientRect();
    return { warmth, x: r.left + r.width / 2 - svg.getBoundingClientRect().left };
  });
  if (probe.x === null) return { ...probe, ink: 0, rows: 0 };

  const buf = await page.screenshot({
    clip: { x: box.x, y: box.y, width: box.width, height: axisH },
  });
  const png = PNG.sync.read(buf);
  const scale = png.width / box.width;
  let ink = 0;
  const rows = new Set<number>();
  const lo = Math.round((probe.x - HALF_WINDOW_PX) * scale);
  const hi = Math.round((probe.x + HALF_WINDOW_PX) * scale);
  for (let y = 0; y < png.height; y++) {
    for (let x = lo; x <= hi; x++) {
      if (x < 0 || x >= png.width) continue;
      const i = (y * png.width + x) * 4;
      const warm = png.data[i] - png.data[i + 1];
      if (warm < 8) continue; // neutral or cool: not the marker
      ink += Math.min(1, warm / probe.warmth);
      rows.add(y);
    }
  }
  return { warmth: probe.warmth, x: probe.x, ink: ink / (scale * scale), rows: rows.size / scale };
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
    // Asserted, not assumed: every bound below is a multiple of this, so a
    // band that measured 0 would quietly make all of them vacuous.
    expect(axisH).toBeGreaterThan(30);

    const { warmth, x, ink, rows } = await todayInkInHeader(page, axisH);
    // The rule has to be in the DOM to be measured at all, and the estimator
    // divides by `warmth`: a token with no red/green split would read every
    // warm pixel as full coverage.
    expect(x, 'the today rule is in the axis').not.toBeNull();
    expect(warmth).toBeGreaterThan(60);

    // A 1px rule lays down one pixel of ink per scanline wherever its centre
    // falls, so the whole band's worth is about axisH. Measured: 49.5 over a
    // 48px band. The upper bound is what says "a rule": a 46x16 pill drawn
    // here would put roughly twice this much ink into the same window.
    expect(ink).toBeGreaterThan(axisH * 0.6);
    expect(ink).toBeLessThan(axisH * 1.8);
    // And it crosses the WHOLE band — this is the clip fact of SRS-TAX-040,
    // and the one assertion a 16px-tall pill cannot satisfy however it is
    // coloured. The defect this test exists for left the top of the band
    // unpainted.
    expect(rows).toBeGreaterThan(axisH * 0.95);

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
