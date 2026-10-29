import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-074, SRS-EDIT-075 — progress is a drag, and the steppers are gone.
 *
 * SR-IXC-01 forbids "stepper buttons, +/- incrementers, or similar discrete
 * controls for continuous values" in those words, and the two ten-percent
 * buttons in the command bar were the only way to set progress at all: "Right
 * now the progress completion, I can only do it with the plus 10, plus 20.
 * This is not actually very, very good."
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

/** The widest bar on screen, so a percentage has pixels to be read in. */
async function widestBar(page: Page) {
  const bars = page.locator('.task-bar-body');
  const count = await bars.count();
  let best: { box: { x: number; y: number; width: number; height: number }; id: string } | null = null;
  for (let i = 0; i < count; i++) {
    const box = (await bars.nth(i).boundingBox())!;
    if (!best || box.width > best.box.width) {
      best = { box, id: (await bars.nth(i).getAttribute('data-pp-id'))! };
    }
  }
  return best!;
}

test.describe('progress by dragging the fill edge', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  test('the handle sets a percentage, with a live readout while it is held', async ({ page }) => {
    await ready(page);
    const { box, id } = await widestBar(page);
    await page.locator(`.task-bar-body[data-pp-id="${id}"]`).click();

    const handle = page.locator(`[data-pp-kind="task"][data-pp-id="${id}"][data-pp-sub="progress"]`);
    await expect(handle).toHaveCount(1);
    const hbox = (await handle.boundingBox())!;

    await page.mouse.move(hbox.x + hbox.width / 2, hbox.y + hbox.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width * 0.75, hbox.y + hbox.height / 2, { steps: 8 });

    // SR-IXC-06: a percentage beside the handle, while the drag is in flight.
    const readout = page.locator(`[data-pp-progress="${id}"]`);
    await expect(readout).toBeVisible();
    // The value the drag pulled to, not merely "some percentage": the old
    // `/^\d{1,3}%$/` was satisfied by the value the bar already held, so it
    // could not tell a working handle from one that never moved. A couple of
    // points of slack for sub-pixel box geometry; pctFromX rounds to whole
    // percent, so this is as tight as the readout can be read.
    const pulled = Number((await readout.textContent())!.replace('%', ''));
    expect(pulled).toBeGreaterThanOrEqual(73);
    expect(pulled).toBeLessThanOrEqual(77);

    await page.mouse.up();
    // And nothing survives the gesture (SR-IXC-10).
    await expect(readout).toHaveCount(0);
  });

  test('the fill ends where the handle was let go', async ({ page }) => {
    await ready(page);
    const { box, id } = await widestBar(page);
    await page.locator(`.task-bar-body[data-pp-id="${id}"]`).click();

    const handle = page.locator(`[data-pp-kind="task"][data-pp-id="${id}"][data-pp-sub="progress"]`);
    const hbox = (await handle.boundingBox())!;
    const targetX = box.x + box.width * 0.6;

    await page.mouse.move(hbox.x + hbox.width / 2, hbox.y + hbox.height / 2);
    await page.mouse.down();
    await page.mouse.move(targetX, hbox.y + hbox.height / 2, { steps: 8 });
    await page.mouse.up();

    // Scoped to the bar that was dragged. `.first()` resolved to whichever
    // task comes first in the document — the sample's, at 100% and never
    // touched — so this measured the distance from THAT bar's fill edge to a
    // point 60% along a different bar, which is where the reported 566px came
    // from.
    const fill = (await page.locator(`.bar-progress-fill[data-pp-fill="${id}"]`).boundingBox())!;
    // The gesture, the preview and the commit are one value (SRS-EDIT-002).
    // The tolerance is tied to the percent quantum: pctFromX rounds to whole
    // percent, so one percent of this bar is the floor on how close it can land.
    expect(Math.abs(fill.x + fill.width - targetX)).toBeLessThan(box.width / 100 + 2);
  });

  test('a date drag is not a progress drag', async ({ page }) => {
    await ready(page);
    const { box, id } = await widestBar(page);
    await page.locator(`.task-bar-body[data-pp-id="${id}"]`).click();
    // Same scoping, same reason: compared against `.first()` this test was
    // measuring a bar nobody had touched, so its assertion was 0 < 4 by
    // construction — green while checking nothing.
    const fillBefore = (await page.locator(`.bar-progress-fill[data-pp-fill="${id}"]`).boundingBox())!;

    // Press the body well away from the handle and move it.
    await page.mouse.move(box.x + 4, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + 44, box.y + box.height / 2, { steps: 6 });
    await page.mouse.up();

    const fillAfter = (await page.locator(`.bar-progress-fill[data-pp-fill="${id}"]`).boundingBox())!;
    // The fill travelled with the bar; its proportion of the bar did not change.
    expect(Math.abs(fillAfter.width - fillBefore.width)).toBeLessThan(4);
  });
});
