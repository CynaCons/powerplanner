import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * The live date readout (SRS-TAX-058, SR-IXC-03, SR-IXC-04).
 *
 * SR-IXC-03 and SR-IXC-04 have required a date pill on every date drag since
 * v2.6.2 and the web surface has never had one: the bar moved and the planner
 * learned the committed date only after letting go. The pill reads the snapped
 * preview, so what it says is what gets written — the two drifting apart is a
 * defect this suite has caught before (SRS-EDIT-002).
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

const pillText = (page: Page) => page.locator('.axis-drag-pill-text').first().textContent();

test.describe('drag date pill', () => {
  test.skip(DESKTOP_ONLY, 'dragging needs a pointer and a plot');

  test('a pill follows a bar drag and reads a date', async ({ page }) => {
    await ready(page);
    const bar = page.locator('[data-pp-kind="task"]').first();
    const box = (await bar.boundingBox())!;

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 90, box.y + box.height / 2, { steps: 6 });

    await expect(page.locator('.axis-drag-pill')).toBeVisible();
    // A range drag names both ends (SR-IXC-03).
    expect(await pillText(page)).toMatch(/→/);

    await page.mouse.up();
    // Nothing survives the gesture (SR-IXC-10).
    await expect(page.locator('.axis-drag-pill')).toHaveCount(0);
  });

  test('a milestone drag names the single target date', async ({ page }) => {
    await ready(page);
    const ms = page.locator('[data-pp-kind="milestone"]').first();
    const box = (await ms.boundingBox())!;

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 70, box.y + box.height / 2, { steps: 6 });

    const text = await pillText(page);
    expect(text).not.toMatch(/→/); // one date, not a range (SR-IXC-04)
    expect(text).toMatch(/\d/);
    await page.mouse.up();
  });

  test('the pill says the date the release commits, not the pointer date', async ({ page }) => {
    await ready(page);
    const ms = page.locator('[data-pp-kind="milestone"]').first();

    /**
     * The committed date, read from the details dialog.
     *
     * NOT from the command bar: it carried the date only as the ±1d steppers
     * SR-IXC-01 forbids and SRS-EDIT-075 removed, so since v2.26.0 the bar
     * holds no date at all and the old assertion was matching a day number
     * against "…ScaleDWMQY1.9px/d". The dialog is where precise dates live
     * now, which is what SR-IXC-02 reserves a secondary editor for.
     */
    const committedDate = async () => {
      await ms.click();
      await page.locator('.cmdbar').getByLabel('Milestone details').click();
      const field = page.locator('[role="dialog"] input[type="date"]').first();
      await expect(field).toBeVisible();
      const value = await field.inputValue();
      await page.keyboard.press('Escape');
      return value;
    };

    const before = await committedDate();
    expect(before).toMatch(/^\d{4}-\d{2}-\d{2}$/);

    const box = (await ms.boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 55, box.y + box.height / 2, { steps: 6 });
    const promised = await pillText(page);
    await page.mouse.up();

    const after = await committedDate();
    // The drag moved it at all — without this, a gesture that did nothing
    // would pass, since the pill would then promise the date already there.
    expect(after).not.toBe(before);
    // And it moved it to the day the pill promised, which is the requirement.
    const day = (promised ?? '').match(/\d{1,2}/)?.[0];
    expect(day).toBeTruthy();
    expect(Number(after.slice(8, 10))).toBe(Number(day));
  });

  test('the pill is never cut off at the edge of the plot', async ({ page }) => {
    await ready(page);
    const svg = page.locator('.chart-area > svg').first();
    const sbox = (await svg.boundingBox())!;
    const bar = page.locator('[data-pp-kind="task"]').first();
    const box = (await bar.boundingBox())!;

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    // Drag hard against the right edge.
    await page.mouse.move(sbox.x + sbox.width - 4, box.y + box.height / 2, { steps: 8 });
    const pill = (await page.locator('.axis-drag-pill').boundingBox())!;
    expect(pill.x + pill.width).toBeLessThanOrEqual(sbox.x + sbox.width + 1);
    expect(pill.x).toBeGreaterThanOrEqual(sbox.x - 1);
    await page.mouse.up();
  });
});
