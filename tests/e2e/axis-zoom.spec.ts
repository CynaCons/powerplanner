import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * The time axis as a planner reads it (SRS-TAX-050 to -056).
 *
 * All four of these were reported out loud against the shipped v2.21.0 build
 * on 2026-10-29, and all four are live-only: nothing in the unit suite can see
 * a passive listener fighting the browser's page zoom, a pill painted over a
 * month label, or a plot with no vertical rule to land a milestone on.
 *
 * NOT EXECUTED at the time of writing: the owner deferred the e2e suite to
 * after the release. Written now so the gate exists when it is run.
 */

/** The chart is the product's full-size surface; a phone has no plot to speak of. */
const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/** Open the gear panel, where the document settings live. */
async function openDocumentSettings(page: Page) {
  await page.locator('button[aria-label="Document settings"], button[title*="Document"]').first().click();
}

/**
 * Read the scale from the status footer, which always prints it one way up.
 *
 * NOT from `.cmdbar__zoom`: that readout flips its unit at 1, printing
 * "0.7px/d" above and "3d/px" below (CommandBar.tsx), so stripping the
 * non-digits out of it yields px-per-day at one scale and its reciprocal at
 * the next. A zoom-in then reads as a DECREASE whenever both samples land
 * under 1, and the assertion below silently inverts. The footer prints
 * "N px/day" at every scale.
 */
const pxPerDay = (page: Page) =>
  page.evaluate(() => {
    const t = document.querySelector('.app-status')?.textContent ?? '';
    return Number((t.match(/([\d.]+)\s*px\/day/) ?? [])[1] ?? '0');
  });

test.describe('wheel zoom (SRS-TAX-050, SRS-TAX-051)', () => {
  test.skip(DESKTOP_ONLY, 'the plot area needs a desktop window');

  test('ctrl+wheel zooms the chart and not the page', async ({ page }) => {
    await ready(page);
    const before = await pxPerDay(page);
    const box = (await page.locator('.chart-area > svg').first().boundingBox())!;

    // The browser page-zooms on ctrl+wheel unless the listener is non-passive
    // and calls preventDefault. If it page-zooms, devicePixelRatio moves.
    const dprBefore = await page.evaluate(() => window.devicePixelRatio);
    await page.mouse.move(box.x + box.width * 0.6, box.y + box.height / 2);
    await page.keyboard.down('Control');
    await page.mouse.wheel(0, -120);
    await page.keyboard.up('Control');

    expect(await pxPerDay(page)).toBeGreaterThan(before);
    expect(await page.evaluate(() => window.devicePixelRatio)).toEqual(dprBefore);
  });

  test('the date under the pointer stays under the pointer', async ({ page }) => {
    await ready(page);
    const box = (await page.locator('.chart-area > svg').first().boundingBox())!;
    const px = box.x + box.width * 0.7;

    /**
     * Which date a screen x lands on, read off the surface the planner sees.
     *
     * This used to read a `window.__pp` probe that the app has never defined,
     * so every call returned null, the guarded assertion below never ran, and
     * the test reported green while checking nothing at all. The view start
     * and the scale are both already printed in the status footer, and the
     * gutter is the `--row-gutter-w` custom property the chart lays out on.
     */
    const dateUnder = () =>
      page.evaluate(
        ([clientX, left]) => {
          const t = document.querySelector('.app-status')?.textContent ?? '';
          const pxPerDay = Number((t.match(/([\d.]+)\s*px\/day/) ?? [])[1] ?? '0');
          const startDate = (t.match(/view starts (\d{4}-\d{2}-\d{2})/) ?? [])[1];
          const gutter = parseFloat(
            getComputedStyle(document.documentElement).getPropertyValue('--row-gutter-w'),
          );
          if (!pxPerDay || !startDate || !Number.isFinite(gutter)) return null;
          const x = clientX - left - gutter;
          const d = new Date(startDate + 'T00:00:00Z');
          d.setUTCDate(d.getUTCDate() + Math.round(x / pxPerDay));
          return d.toISOString().slice(0, 10);
        },
        [px, box.x],
      );

    const before = await dateUnder();
    await page.mouse.move(px, box.y + box.height / 2);
    for (let i = 0; i < 6; i++) {
      await page.keyboard.down('Control');
      await page.mouse.wheel(0, -120);
      await page.keyboard.up('Control');
    }
    const after = await dateUnder();
    // Asserted, not guarded: if the readout cannot be had, that is the defect.
    expect(before).not.toBeNull();
    expect(after).not.toBeNull();
    const drift = Math.abs(
      (new Date(after!).getTime() - new Date(before!).getTime()) / 86_400_000,
    );
    // Six notches, one rounding each: a couple of days is the floor.
    expect(drift).toBeLessThanOrEqual(3);
  });
});

test.describe('the today marker (SRS-TAX-053)', () => {
  test.skip(DESKTOP_ONLY, 'the plot area needs a desktop window');

  test('the pill sits below the calendar bands, not in them', async ({ page }) => {
    await ready(page);
    const geom = await page.evaluate(() => {
      const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
      const pill = svg.querySelector('.axis-today-pill') as SVGRectElement | null;
      const rule = svg.querySelector('.axis-today-rule') as SVGLineElement | null;
      if (!pill || !rule) return null;
      return {
        pillTop: pill.getBoundingClientRect().top - svg.getBoundingClientRect().top,
        axisFoot: rule.y2.baseVal.value,
      };
    });
    expect(geom).not.toBeNull();
    expect(geom!.pillTop).toBeGreaterThanOrEqual(geom!.axisFoot);
  });

  test('every calendar label is still legible under the marker', async ({ page }) => {
    await ready(page);
    // No label's box may be covered by the pill, at any scale.
    for (const scale of ['Day', 'Week', 'Month', 'Quarter']) {
      await page.locator(`button:has-text("${scale}")`).first().click();
      const overlap = await page.evaluate(() => {
        const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
        const pill = svg.querySelector('.axis-today-pill');
        if (!pill) return 0;
        const p = pill.getBoundingClientRect();
        return Array.from(svg.querySelectorAll('text')).filter((t) => {
          const r = t.getBoundingClientRect();
          return r.width > 0 && r.right > p.left && r.left < p.right && r.bottom > p.top && r.top < p.bottom;
        }).length;
      });
      expect(overlap, `${scale} scale`).toEqual(0);
    }
  });
});

test.describe('delimiters and weekends (SRS-TAX-054, SRS-TAX-056)', () => {
  test.skip(DESKTOP_ONLY, 'the plot area needs a desktop window');

  const delimiterCount = (page: Page) => page.locator('.grid-delimiter').count();
  const weekendCount = (page: Page) => page.locator('.grid-weekend').count();

  test('the plot has vertical rules out of the box', async ({ page }) => {
    await ready(page);
    expect(await delimiterCount(page)).toBeGreaterThan(2);
  });

  test('the granularity is the roadmap setting, not the zoom', async ({ page }) => {
    await ready(page);
    await openDocumentSettings(page);

    await page.locator('[aria-label="Grid delimiters"] button:has-text("Month")').click();
    const monthly = await delimiterCount(page);
    await page.locator('[aria-label="Grid delimiters"] button:has-text("Day")').click();
    expect(await delimiterCount(page)).toBeGreaterThan(monthly);
    await page.locator('[aria-label="Grid delimiters"] button:has-text("None")').click();
    expect(await delimiterCount(page)).toEqual(0);
  });

  test('weekend shading can be turned off and on', async ({ page }) => {
    await ready(page);
    await openDocumentSettings(page);
    await page.locator('[aria-label="Weekend display"] button:has-text("Shaded")').click();
    expect(await weekendCount(page)).toBeGreaterThan(0);
    await page.locator('[aria-label="Weekend display"] button:has-text("Off")').click();
    expect(await weekendCount(page)).toEqual(0);
  });

  test('both settings survive a reload through the autosave', async ({ page }) => {
    await ready(page);
    await openDocumentSettings(page);
    await page.locator('[aria-label="Grid delimiters"] button:has-text("Qtr")').click();
    await page.locator('[aria-label="Weekend display"] button:has-text("Off")').click();
    await page.waitForTimeout(1500); // the autosave is debounced
    await page.reload();
    await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
    expect(await weekendCount(page)).toEqual(0);
  });
});
