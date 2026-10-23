import { test, expect } from '@playwright/test';

/**
 * C2 (PLAN.md v2.13.2): everything the inspector could edit is reachable from
 * the command bar. This is the gate that lets C3 delete the inspector column
 * without losing a capability.
 */
test.describe('selection editing in the command bar', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    await page.locator('[data-pp-kind="task"]').first().click();
  });

  test('the label is editable in the bar', async ({ page }) => {
    const input = page.locator('.cmdbar').getByLabel('Label');
    await expect(input).toBeVisible();
    await input.fill('Renamed from the bar');
    await expect(page.locator('.chart-area')).toContainText('Renamed from the bar');
  });

  test('dates and percent have precise nudges', async ({ page }) => {
    const bar = page.locator('.cmdbar');
    await expect(bar.getByLabel('Move a day later')).toBeVisible();
    await expect(bar.getByLabel('Ten percent more complete')).toBeVisible();
    await bar.getByLabel('Ten percent more complete').click();
    // The popover reports the value, so read it back there.
    await bar.getByLabel('Edit task').click();
    await expect(page.locator('[role="dialog"]')).toContainText('%');
  });

  test('dates, percent, notes and label position live in Edit', async ({ page }) => {
    await page.locator('.cmdbar').getByLabel('Edit task').click();
    const pop = page.locator('[role="dialog"]');
    await expect(pop).toContainText('Row');
    await expect(pop).toContainText('Start');
    await expect(pop).toContainText('End');
    await expect(pop).toContainText('Notes');
    await expect(pop.getByRole('radiogroup', { name: 'Label position' })).toBeVisible();

    await pop.locator('textarea').fill('A note written through the bar');
    await expect(pop.locator('textarea')).toHaveValue('A note written through the bar');
  });

  test('colour is pickable, and offers the stream palette', async ({ page }) => {
    await expect(page.locator('.cmdbar').getByRole('button', { name: 'Prussian' })).toBeVisible();
  });

  test('a task can be deleted from the bar', async ({ page }) => {
    const before = await page.locator('.app-status').innerText();
    await page.locator('.cmdbar').getByLabel('Delete task').click();
    await expect(page.locator('.app-status')).not.toHaveText(before);
  });
});
