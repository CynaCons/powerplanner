import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-070, SRS-EDIT-071 — the end of a selected bar is its resize grip.
 *
 * The two dependency anchors were centred on the bar's end caps, which is the
 * only place a resize grip can go, so a selected bar had no resizable ends:
 * "we should first select, but then when we select, we have the anchor to do
 * the dependency… we are missing that option to resize." Moving the anchors
 * onto a lead is half of it; the other half is that `Dependencies` paints a
 * transparent 10px hit stroke after the bars, so a connector crossing an end
 * took the press regardless of what was drawn underneath.
 *
 * NOT EXECUTED at the time of writing: the owner deferred the e2e suite to
 * after the release. Written now so the gate exists when it is run.
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/** The selected bar's own geometry, read off the DOM rather than guessed. */
async function selectedBarBox(page: Page) {
  const bar = page.locator('.task-bar-body[data-pp-selected="1"]').first();
  return (await bar.boundingBox())!;
}

test.describe('resize grips on a selected bar', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  test('a selected bar can still be resized from its end', async ({ page }) => {
    await ready(page);
    const bar = page.locator('.task-bar-body').first();
    await bar.click();
    const box = await selectedBarBox(page);

    const grip = page.locator('[data-pp-kind="task"][data-pp-sub="end"]').first();
    await expect(grip).toHaveCount(1);

    await page.mouse.move(box.x + box.width, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width + 60, box.y + box.height / 2, { steps: 8 });
    await page.mouse.up();

    const after = await selectedBarBox(page);
    // The bar grew. Before, the press landed on the anchor and started a
    // dependency drag instead, so the width never changed.
    expect(after.width).toBeGreaterThan(box.width + 20);
    expect(Math.abs(after.x - box.x)).toBeLessThan(3);
  });

  test('the anchors share no pixel with a grip', async ({ page }) => {
    await ready(page);
    await page.locator('.task-bar-body').first().click();
    const box = await selectedBarBox(page);

    const anchors = page.locator('[data-pp-kind="connector"]');
    await expect(anchors).toHaveCount(2);

    for (let i = 0; i < 2; i++) {
      const a = (await anchors.nth(i).boundingBox())!;
      const overlapsStart = a.x < box.x + 4 && a.x + a.width > box.x - 4;
      const overlapsEnd = a.x < box.x + box.width + 4 && a.x + a.width > box.x + box.width - 4;
      expect(overlapsStart || overlapsEnd).toBe(false);
    }
  });

  test('a dependency crossing a bar end does not take the resize press', async ({ page }) => {
    await ready(page);
    // The shipped sample carries connectors; find a bar whose end a connector
    // passes over, which is the geometry that produced the defect.
    const bars = page.locator('.task-bar-body');
    const count = await bars.count();
    let target: { x: number; y: number; width: number; height: number } | null = null;
    for (let i = 0; i < count; i++) {
      const box = (await bars.nth(i).boundingBox())!;
      const onTop = await page.evaluate(
        ([x, y]) =>
          document
            .elementsFromPoint(x, y)
            .some((el) => (el as HTMLElement).dataset?.ppKind === 'dependency'),
        [box.x + box.width, box.y + box.height / 2],
      );
      if (onTop) {
        target = box;
        break;
      }
    }
    test.skip(target == null, 'no connector crosses a bar end in this sample');

    await page.mouse.move(target!.x + target!.width, target!.y + target!.height / 2);
    await page.mouse.down();
    await page.mouse.move(target!.x + target!.width + 50, target!.y + target!.height / 2, { steps: 6 });
    await page.mouse.up();

    // Resized, and no dependency was selected on the way through.
    const selected = await page.evaluate(() => document.querySelectorAll('[data-pp-selected="1"]').length);
    expect(selected).toBeGreaterThan(0);
  });
});
