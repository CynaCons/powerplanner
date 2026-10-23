import { test, expect } from '@playwright/test';

/**
 * Label placement is reachable by hand (SRS-ABR-019) and a label that steps off
 * the line is tied back to its own marker by a leader.
 */
test.describe('milestone label placement', () => {
  test('a user can pin a label above, and it gets a leader', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const ms = page.locator('[data-pp-id="ms-swfreeze"]');
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

    const ms = page.locator('[data-pp-id="ms-swfreeze"]');
    await ms.click({ button: 'right', force: true });
    await page.locator('[role="menu"]').first().getByRole('radio', { name: 'Right' }).click();
    await expect(page.locator('[data-pp-leader="ms-swfreeze"]')).toHaveCount(0);
  });
});
