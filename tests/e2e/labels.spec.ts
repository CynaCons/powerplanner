import { test, expect } from '@playwright/test';

/**
 * Label placement is reachable by hand (SRS-ABR-019) and a label that steps off
 * the line is tied back to its own marker by a leader.
 */
test.describe('milestone label placement', () => {
  test('a user can pin a label above, and it gets a leader', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const ms = page.locator('[data-pp-kind="milestone"][data-pp-id="ms-swfreeze"]');
    await expect(page.locator('[data-pp-leader="ms-swfreeze"]')).toHaveCount(0);

    await ms.click({ button: 'right', force: true });
    const menu = page.locator('[role="menu"]').first();
    await expect(menu).toContainText('Label position');
    await menu.getByRole('radio', { name: 'Above' }).click();

    // Above is off the line, so the label is bound back to its diamond.
    await expect(page.locator('[data-pp-leader="ms-swfreeze"]')).toHaveCount(1);
  });

  test('right needs no leader, because it sits against its own marker', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const ms = page.locator('[data-pp-kind="milestone"][data-pp-id="ms-swfreeze"]');
    await ms.click({ button: 'right', force: true });
    await page.locator('[role="menu"]').first().getByRole('radio', { name: 'Right' }).click();
    await expect(page.locator('[data-pp-leader="ms-swfreeze"]')).toHaveCount(0);
  });
});

test.describe('dragging a label', () => {
  test('a label can be dragged to a place of the user own choosing', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const label = page.locator('[data-pp-kind="label"][data-pp-id="ms-swfreeze"]');
    await expect(label).toHaveCount(1);
    const before = await label.boundingBox();
    if (!before) throw new Error('label has no box');

    await page.mouse.move(before.x + before.width / 2, before.y + before.height / 2);
    await page.mouse.down();
    await page.mouse.move(before.x + before.width / 2 + 70, before.y + before.height / 2 - 40, { steps: 8 });
    await page.mouse.up();

    const after = await label.boundingBox();
    if (!after) throw new Error('label vanished after the drag');
    expect(Math.round(after.x - before.x)).toBeGreaterThan(40);
    expect(Math.round(after.y - before.y)).toBeLessThan(-20);

    // A label that has been moved always keeps a leader back to its marker.
    await expect(page.locator('[data-pp-leader="ms-swfreeze"]')).toHaveCount(1);
  });

  test('picking a position releases a dragged label', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const label = page.locator('[data-pp-kind="label"][data-pp-id="ms-swfreeze"]');
    const home = await label.boundingBox();
    if (!home) throw new Error('label has no box');
    await page.mouse.move(home.x + home.width / 2, home.y + home.height / 2);
    await page.mouse.down();
    await page.mouse.move(home.x + home.width / 2 + 70, home.y + home.height / 2 - 40, { steps: 6 });
    await page.mouse.up();

    await page.locator('[data-pp-kind="milestone"][data-pp-id="ms-swfreeze"]').click({ button: 'right', force: true });
    await page.locator('[role="menu"]').first().getByRole('radio', { name: 'Right' }).click();

    const back = await label.boundingBox();
    if (!back) throw new Error('label vanished');
    expect(Math.abs(back.x - home.x)).toBeLessThan(8);
    expect(Math.abs(back.y - home.y)).toBeLessThan(8);
  });
});
