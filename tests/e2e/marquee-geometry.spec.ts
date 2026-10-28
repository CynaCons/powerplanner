import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-033 — one geometry, read at the moment of the event.
 *
 * Four hand-rolled copies of the same subtraction decided where a pointer was:
 * the press used the live gutter, the move used render 1's (the chart has no
 * width on the first pass, so the gutter resolves to its 120px minimum before
 * the ResizeObserver makes it 200), the hit test compared against a `layout`
 * captured before the initial auto-fit, and only the paint read `scrollY`.
 *
 * Observed red on 090ddae:
 *   band     drawn 80px wider than the swept rectangle — exactly
 *            ROW_GUTTER_W_BASE 200 - ROW_GUTTER_W_MIN 120
 *   scrolled band drawn ~600px above the pointer after one scroll
 *   selection 2 bars selected where the drawn band covered 5
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/**
 * A press point with no chart element under it, chosen so that sweeping by
 * (dx, dy) from there crosses some bars — a sweep over blank paper proves
 * nothing about which elements it takes.
 */
async function emptyPoints(page: Page, dx = 0, dy = 0, want = 1) {
  const found = await page.evaluate(([sx, sy, n]) => {
    const svg = document.querySelector('.chart-area > svg') as SVGSVGElement;
    const r = svg.getBoundingClientRect();
    const bars = Array.from(document.querySelectorAll('.task-bar-body')).map((el) =>
      el.getBoundingClientRect(),
    );
    const covered = (x: number, y: number) => {
      const rect = { x: Math.min(x, x + sx), y: Math.min(y, y + sy), w: Math.abs(sx), h: Math.abs(sy) };
      return bars.filter(
        (b) => b.right >= rect.x && b.left <= rect.x + rect.w && b.bottom >= rect.y && b.top <= rect.y + rect.h,
      ).length;
    };
    const out: { x: number; y: number }[] = [];
    for (let y = 100; y < r.height - 90; y += 6) {
      for (let x = 230; x < r.width - 60; x += 8) {
        const cx = r.left + x;
        const cy = r.top + y;
        const el = document.elementFromPoint(cx, cy);
        if (el?.closest('[data-pp-kind]')) continue;
        if (sx !== 0 && covered(cx, cy) < 3) continue;
        if (out.some((p) => Math.abs(p.x - cx) < 120 && Math.abs(p.y - cy) < 80)) continue;
        out.push({ x: cx, y: cy });
        if (out.length >= n) return out;
      }
    }
    return out;
  }, [dx, dy, want] as [number, number, number]);
  expect(found.length, 'the plot has empty spots to start a sweep from').toBeGreaterThan(0);
  return found;
}

/** Tasks whose bar intersects a client rectangle — what a sweep should take. */
async function tasksUnder(page: Page, rect: { x: number; y: number; width: number; height: number }) {
  return page.evaluate((r) => {
    const ids: string[] = [];
    for (const el of document.querySelectorAll('.task-bar-body')) {
      const b = el.getBoundingClientRect();
      const hit = b.right >= r.x && b.left <= r.x + r.width && b.bottom >= r.y && b.top <= r.y + r.height;
      if (hit) ids.push((el as SVGElement).getAttribute('data-pp-id')!);
    }
    return ids.sort();
  }, rect);
}

async function selectedTasks(page: Page) {
  return page.evaluate(() =>
    Array.from(document.querySelectorAll('.task-bar-body[data-pp-selected]'))
      .map((el) => el.getAttribute('data-pp-id')!)
      .sort(),
  );
}

/**
 * Sweep a marquee and report the band the chart drew while it was open, plus
 * the client rectangle the pointer actually described.
 */
async function sweep(page: Page, from: { x: number; y: number }, dx: number, dy: number) {
  await page.keyboard.press('r');
  await page.mouse.move(from.x, from.y);
  await page.mouse.down();
  await page.mouse.move(from.x + dx, from.y + dy, { steps: 10 });
  const bandRect = page.locator('rect[stroke-dasharray="3 3"]');
  await bandRect.waitFor({ state: 'attached', timeout: 2000 });
  const band = await bandRect.boundingBox();
  const swept = {
    x: Math.min(from.x, from.x + dx),
    y: Math.min(from.y, from.y + dy),
    width: Math.abs(dx),
    height: Math.abs(dy),
  };
  const expected = await tasksUnder(page, swept);
  await page.mouse.up();
  return { band, swept, expected };
}

function expectSameRect(
  band: { x: number; y: number; width: number; height: number } | null,
  swept: { x: number; y: number; width: number; height: number },
) {
  expect(band, 'the marquee band is drawn').not.toBeNull();
  expect(Math.round(band!.x), 'band left').toBeCloseTo(Math.round(swept.x), -0.5);
  expect(Math.round(band!.y), 'band top').toBeCloseTo(Math.round(swept.y), -0.5);
  expect(Math.round(band!.width), 'band width').toBeCloseTo(Math.round(swept.width), -0.5);
  expect(Math.round(band!.height), 'band height').toBeCloseTo(Math.round(swept.height), -0.5);
}

test.describe('marquee geometry (SRS-EDIT-033)', () => {
  test.skip(DESKTOP_ONLY, 'the gutter only reaches its full width on a desktop window');

  test('the band is drawn where the pointer went', async ({ page }) => {
    await ready(page);
    const [from] = await emptyPoints(page, 260, 120);
    const { band, swept } = await sweep(page, from, 260, 120);
    expectSameRect(band, swept);
  });

  test('the band follows the pointer once the chart is scrolled', async ({ page }) => {
    await ready(page);
    const box = (await page.locator('.chart-area > svg').first().boundingBox())!;
    await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.6);
    await page.mouse.wheel(0, 600);
    await page.waitForTimeout(60);

    const [from] = await emptyPoints(page, 240, 110);
    const { band, swept } = await sweep(page, from, 240, 110);
    expectSameRect(band, swept);
  });

  // One sweep proves little: an 80px error on the moving edge only changes the
  // ANSWER where a bar happens to sit in those 80px. Sweep from several places
  // and in both directions, so the error has somewhere to show.
  for (const [dx, dy] of [[300, 150], [-300, 150], [300, -140], [-260, -120]] as const) {
    test(`a sweep of ${dx}x${dy} takes exactly the bars it covers`, async ({ page }) => {
      await ready(page);
      const starts = await emptyPoints(page, dx, dy, 3);
      for (const from of starts) {
        // A selected bar grows connector handles, which would sit under a
        // start point that was empty a moment ago.
        await page.keyboard.press('Escape');
        const { expected } = await sweep(page, from, dx, dy);
        expect(expected.length, 'the sweep covers some bars').toBeGreaterThan(0);
        expect(await selectedTasks(page), `sweep from ${Math.round(from.x)},${Math.round(from.y)}`).toEqual(expected);
      }
    });
  }

  test('a sweep still takes what it covers after a zoom', async ({ page }) => {
    await ready(page);
    const box = (await page.locator('.chart-area > svg').first().boundingBox())!;
    await page.mouse.move(box.x + box.width * 0.6, box.y + box.height * 0.5);
    await page.keyboard.down('Control');
    for (let i = 0; i < 6; i++) await page.mouse.wheel(0, -200);
    await page.keyboard.up('Control');
    await page.waitForTimeout(80);

    const [from] = await emptyPoints(page, 300, 150);
    const { expected } = await sweep(page, from, 300, 150);
    expect(expected.length, 'the sweep covers some bars').toBeGreaterThan(0);
    expect(await selectedTasks(page)).toEqual(expected);
  });
});
