import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-023/024/025/029 — selecting something shows it, and clearing it
 * stops showing it, without anything else having to happen first.
 *
 * The five chart renderers each read `useSelectionStore((s) => s.isSelected)`.
 * That selector returns the function `create()` built once, so its reference
 * never changes, Zustand's equality check always passes, and the component is
 * never re-rendered. The chrome was correct the whole time and simply never
 * painted: a click selected a bar and drew nothing, Escape cleared it and left
 * the outline on screen, and either only resolved when some unrelated redraw
 * happened along — which is why the highlight appeared mid-drag. The status bar
 * and the command bar subscribe to `items`, so the footer reacted while the
 * chart did not.
 *
 * Observed red on 79ccbdb: after a plain click, 0 bars carried
 * `data-pp-selected`, while the status bar already read "1 selected".
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/** The chrome actually on screen, counted from the DOM rather than inferred. */
async function paintedSelection(page: Page) {
  return page.evaluate(() => ({
    bars: document.querySelectorAll('[data-pp-selected="1"]').length,
    status: document.querySelector('.app-status')?.textContent ?? '',
  }));
}

test.describe('selection paints when it changes', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  test('a plain click paints the bar it selected, with nothing else touched', async ({ page }) => {
    await ready(page);
    const bar = page.locator('.task-bar-body').first();
    await bar.click();

    // No scroll, no drag, no second click: whatever is on screen now is what
    // the user sees after clicking once.
    const after = await paintedSelection(page);
    expect(after.status).toContain('1 selected');
    expect(after.bars).toBe(1);
  });

  test('Escape stops painting it, in the same frame', async ({ page }) => {
    await ready(page);
    await page.locator('.task-bar-body').first().click();
    expect((await paintedSelection(page)).bars).toBe(1);

    await page.keyboard.press('Escape');
    const after = await paintedSelection(page);
    expect(after.status).toContain('0 selected');
    expect(after.bars).toBe(0);
  });

  test('a click on empty chart space clears it too', async ({ page }) => {
    await ready(page);
    await page.locator('.task-bar-body').first().click();
    expect((await paintedSelection(page)).bars).toBe(1);

    // Far below the last row, inside the plot, on nothing.
    const svg = page.locator('.chart-area > svg').first();
    const box = (await svg.boundingBox())!;
    await page.mouse.click(box.x + box.width - 60, box.y + box.height - 40);
    expect((await paintedSelection(page)).bars).toBe(0);
  });

  test('the selected row is banded across the plot, not only the gutter', async ({ page }) => {
    await ready(page);
    await page.locator('.task-bar-body').first().click();

    const band = await page.evaluate(() => {
      const el = document.querySelector('[data-pp-rowband="1"]');
      if (!el) return null;
      const r = el.getBoundingClientRect();
      const svg = document.querySelector('.chart-area > svg')!.getBoundingClientRect();
      return { w: Math.round(r.width), svgW: Math.round(svg.width) };
    });
    expect(band, 'a row band is drawn for the selected row').not.toBeNull();
    // Full width, give or take the rounding of a scrollbar.
    expect(band!.w).toBeGreaterThan(band!.svgW - 8);
  });

  test('the command bar no longer restates the name', async ({ page }) => {
    await ready(page);
    const bar = page.locator('.task-bar-body').first();
    const name = await bar.getAttribute('data-pp-id');
    await bar.click();

    await expect(page.locator('.cmdbar')).toBeVisible();
    expect(await page.locator('.cmdbar__selname').count()).toBe(0);
    expect(name).toBeTruthy();
  });
});
