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

    // Theme moved from the inspector's Appearance section to
    // File → Document settings (PLAN.md v2.13.0 / C0b): it is a once-per-document
    // setting and did not earn a permanent column.
    await openDocumentSettings(page);
    await page.locator('[role="dialog"] [role="radio"]:has-text("Dark")').click();
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');
    await page.locator('[role="dialog"] [role="radio"]:has-text("Light")').click();
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
  });

  test('fiscal year start is reachable from document settings', async ({ page }) => {
    await page.goto('/');
    await openDocumentSettings(page);
    const select = page.locator('[role="dialog"] select');
    await expect(select).toHaveValue('1');
    await select.selectOption('4');
    await expect(select).toHaveValue('4');
  });

  test('no control is rendered in two places at once (v2.13.0 / C0)', async ({ page }) => {
    await page.goto('/');
    // The scale segmented control used to be painted by BOTH the inspector and
    // the tool palette simultaneously, and Fit by both header and palette.
    await expect(page.locator('button', { hasText: /^[DWMQY]$/ })).toHaveCount(5);
    await expect(page.locator('button[title*="Fit" i]')).toHaveCount(1);
    // The retired sections must not come back.
    await expect(page.locator('.inspector')).not.toContainText('Statistics');
    await expect(page.locator('.inspector')).not.toContainText('Appearance');
  });

  test('status bar carries the statistics the inspector used to show', async ({ page }) => {
    await page.goto('/');
    const status = page.locator('.app-status');
    await expect(status).toContainText('6 tasks');
    await expect(status).toContainText('milestones');
    await expect(status).toContainText('span');
    await expect(status).toContainText('% complete');
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

/** Open File → Document settings…, where the relocated document-level settings live. */
async function openDocumentSettings(page: import('@playwright/test').Page) {
  await page.locator('button[title="File menu"]').click();
  await page.locator('[role="menu"] button', { hasText: 'Document settings' }).click();
  await page.locator('[role="dialog"]').waitFor();
}
