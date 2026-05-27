import { test, expect } from '@playwright/test';

test.describe('PowerPlanner smoke', () => {
  test('loads and shows sample chart', async ({ page }) => {
    await page.goto('/');
    // Header brand
    await expect(page.locator('.app-header').getByText('PowerPlanner')).toBeVisible();
    // Inspector visible (in desktop viewport)
    await expect(page.locator('.inspector')).toBeVisible();
    // SVG renders
    await expect(page.locator('.chart-area svg')).toBeVisible();
    // Sample tasks rendered
    await expect(page.locator('text=Wireframes')).toBeVisible();
    // Status bar shows task count
    await expect(page.locator('.app-status')).toContainText('6 tasks');
  });

  test('adds a task via the toolbar', async ({ page }) => {
    await page.goto('/');
    await page.locator('.app-header button[title^="Add task"]').click();
    await expect(page.locator('.app-status')).toContainText('7 tasks');
  });

  test('changes theme to light', async ({ page }) => {
    await page.goto('/');
    // Theme is a segmented radio control; click the Light option in the Appearance section
    await page.locator('.inspector [role="radio"]:has-text("Light")').click();
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
  });
});
