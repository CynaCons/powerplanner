import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-ELEM-051/052/053 — a deadline marker can be reached, moved and renamed.
 *
 * Owner report 2026-10-29, on the shipped v2.21.0 build: "Once I have created
 * a deadline marker, I cannot edit the text, I cannot move it around." He was
 * right in the strongest sense — `Markers` drew a line and a text node with no
 * hit target of any kind, so there was nothing on the chart to click, and the
 * marker's own context menu items were unreachable.
 *
 * This walks it as a first-time user would: insert from the command bar, click
 * the chip in the foot band, drag it, rename it in place.
 *
 * Written with iteration v2.28.0 and NOT EXECUTED in it: the owner deferred the
 * end-to-end suite to after the release (task brief 2026-10-29). The unit
 * cover for the same three rows — the gesture driven through the shipped
 * component — is `tests/unit/markerFoot.test.ts`, which passes.
 */

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/** The chip in the foot band, which is where every marker's name now lives. */
function chip(page: Page) {
  return page.locator('rect[data-pp-kind="marker"][rx="4"]').first();
}

test.describe('a deadline marker is editable (SRS-ELEM-051)', () => {
  test('the chip in the foot band carries the name, and selects on a click', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    await expect(page.locator('text=MARKERS')).toBeVisible();
    await expect(page.getByText('Deadline', { exact: true })).toBeVisible();

    await chip(page).click();
    await expect(page.locator('.app-status')).toContainText('1 selected');
  });

  test('dragging the chip moves the date, and one undo puts it back', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    const before = await chip(page).boundingBox();
    expect(before).not.toBeNull();
    await page.mouse.move(before!.x + before!.width / 2, before!.y + before!.height / 2);
    await page.mouse.down();
    await page.mouse.move(before!.x + before!.width / 2 + 120, before!.y + before!.height / 2, { steps: 8 });
    // The live readout is the requirement, not a nicety (SR-IXC-04).
    await expect(page.locator('rect[rx="9"]')).toBeVisible();
    await page.mouse.up();

    const after = await chip(page).boundingBox();
    expect(after!.x).toBeGreaterThan(before!.x + 40);

    await page.keyboard.press('Control+z');
    const undone = await chip(page).boundingBox();
    expect(Math.abs(undone!.x - before!.x)).toBeLessThan(2);
  });

  test('a double-click renames it in place (SRS-ELEM-053)', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    await chip(page).dblclick();
    const editor = page.locator('input.inline-editor');
    await expect(editor).toBeVisible();
    await editor.fill('Gate review');
    await editor.press('Enter');

    await expect(page.getByText('Gate review', { exact: true })).toBeVisible();
    await expect(editor).toHaveCount(0);
  });

  test('right-click offers the menu that used to be unreachable', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    await chip(page).click({ button: 'right' });
    await expect(page.getByRole('menuitem', { name: /Rename/ })).toBeVisible();
    await expect(page.getByRole('menuitem', { name: /Delete marker/ })).toBeVisible();
  });
});
