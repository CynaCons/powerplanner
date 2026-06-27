import { test, expect } from '@playwright/test';

test.describe('PowerPlanner smoke', () => {
  test('loads and shows sample chart', async ({ page }) => {
    await page.goto('/');
    // Header brand
    await expect(page.locator('.app-header').getByText('PowerPlanner')).toBeVisible();
    // Inspector visible (in desktop viewport)
    await expect(page.locator('.inspector')).toBeVisible();
    // Chart SVG renders (first svg = main chart, second = minimap)
    await expect(page.locator('.chart-area > svg').first()).toBeVisible();
    // Sample tasks rendered
    await expect(page.locator('text=Wireframes').first()).toBeVisible();
    // Status bar shows task count
    await expect(page.locator('.app-status')).toContainText('6 tasks');
  });

  test('adds a task via the toolbar', async ({ page }) => {
    await page.goto('/');
    await page.locator('.app-header button[title^="Add task"]').click();
    await expect(page.locator('.app-status')).toContainText('7 tasks');
  });

  test('defaults to light theme and can switch themes', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
    // Theme is a segmented radio control in the Appearance section.
    await page.locator('.inspector [role="radio"]:has-text("Dark")').click();
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');
    await page.locator('.inspector [role="radio"]:has-text("Light")').click();
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
  });

  test('command palette opens with Cmd+K and runs a command', async ({ page }) => {
    await page.goto('/');
    // Dismiss restore banner if any
    const dismiss = page.locator('[aria-label="Dismiss"]').first();
    if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
    await page.keyboard.press('ControlOrMeta+k');
    await expect(page.locator('[aria-label="Command palette"]')).toBeVisible();
    // Filter and execute
    await page.keyboard.type('milestone');
    await expect(page.locator('[aria-label="Command palette"]')).toContainText('New milestone');
    await page.keyboard.press('Enter');
    // The palette should close and we should now have 3 milestones
    await expect(page.locator('[aria-label="Command palette"]')).toHaveCount(0);
    await expect(page.locator('.app-status')).toContainText('3 milestones');
  });
});
