import { test, expect } from '@playwright/test';

test.describe('PowerRoadmap smoke', () => {
  test('loads and shows sample chart', async ({ page }) => {
    await page.goto('/');
    // Header brand
    await expect(page.locator('.app-header').getByText('PowerRoadmap')).toBeVisible();
    // The inspector column is gone; editing lives in the command bar (C3).
    await expect(page.locator('.inspector')).toHaveCount(0);
    // Chart SVG renders. There is one now: the minimap that used to follow it
    // was removed in v2.28.0 (SRS-ELEM-060).
    await expect(page.locator('.chart-area > svg').first()).toBeVisible();
    // Sample tasks rendered
    await expect(page.locator('text=Stakeholder requirements').first()).toBeVisible();
    // Status bar shows task count
    await expect(page.locator('.app-status')).toContainText('33 tasks');
  });

  test('adds a task from the command bar', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    // Inserting lives in the bar's INSERT group now, not in the header, where
    // it was a duplicate that dropped everything on row one (DECISIONS.md D7).
    await page.locator('.cmdbar').getByTitle('Add task (T)').click();
    await expect(page.locator('.app-status')).toContainText('34 tasks');
  });

  test('the header no longer duplicates the insert actions', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('.app-header').getByTitle(/^Add /)).toHaveCount(0);
  });

  test('defaults to light theme and can switch themes', async ({ page }) => {
    await page.goto('/');
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');

    // Theme now lives in the command bar's document context (v2.13.1 / C1f).
    await openDocumentSettings(page);
    await page.locator('.cmdbar [role="radio"]:has-text("Dark")').click();
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'dark');
    await page.locator('.cmdbar [role="radio"]:has-text("Light")').click();
    await expect(page.locator('html')).toHaveAttribute('data-theme', 'light');
  });

  test('fiscal year start is reachable from document settings', async ({ page }) => {
    await page.goto('/');
    await openDocumentSettings(page);
    const select = page.locator('.cmdbar select');
    await expect(select).toHaveValue('1');
    await select.selectOption('4');
    await expect(select).toHaveValue('4');
  });

  // Both of these asserted the horizontal bar's mode switch: one strip, and
  // tools, selection and document settings taking turns in it. The panel's
  // sections coexist (SRS-LAY-030), so the gear has nothing to take the place
  // of and no longer clears the selection to make room.
  test('opening document settings keeps the selection', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg .task-bar-body').first().click();
    await expect(page.locator('.app-status')).toContainText('1 selected');
    await page.locator('.cmdbar button[aria-label="Document settings"]').click();
    await expect(page.locator('.cmdbar [role="radiogroup"][aria-label="Theme"]')).toBeVisible();
    await expect(page.locator('.app-status')).toContainText('1 selected');
  });

  test('a section opens and closes, and the tools stay reachable throughout', async ({ page }) => {
    await page.goto('/');
    const tools = page.locator('.cmdbar button[title^="Pan"]');
    await expect(tools).toBeVisible();
    // Selecting something adds a section rather than replacing one.
    await page.locator('.chart-area svg .task-bar-body').first().click();
    await expect(page.locator('[data-pp-section="selection"]')).toBeVisible();
    await expect(tools).toBeVisible();

    const head = page.locator('[data-pp-section="tools"] .cmdbar__section-head');
    await expect(head).toHaveAttribute('aria-expanded', 'true');
    await head.click();
    await expect(head).toHaveAttribute('aria-expanded', 'false');
    // Closed means unmounted: a collapsed section must hold no focusable
    // control, or Tab walks through things nobody can see.
    await expect(tools).toHaveCount(0);
    await head.click();
    await expect(tools).toBeVisible();
  });

  test('nothing in the panel escapes it (SRS-LAY-031)', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg .task-bar-body').first().click();
    await page.locator('.cmdbar button[aria-label="Document settings"]').click();
    await expect(page.locator('.cmdbar [role="radiogroup"][aria-label="Theme"]')).toBeVisible();
    // The bar this replaced measured 1210px of content inside 986px, and the
    // px/day readout sat 99px past its own rounded edge.
    const escaping = await page.evaluate(() => {
      const root = document.querySelector('.cmdbar');
      if (!root) return ['no panel'];
      const box = root.getBoundingClientRect();
      const out: string[] = [];
      for (const el of root.querySelectorAll('*')) {
        const r = el.getBoundingClientRect();
        if (!r.width && !r.height) continue;
        if (r.right > box.right + 0.5 || r.left < box.left - 0.5) {
          out.push(`${el.tagName.toLowerCase()}.${String(el.className).slice(0, 30)}`);
        }
      }
      if (root.scrollWidth > root.clientWidth + 1) out.push('panel scrolls sideways');
      return out;
    });
    expect(escaping).toEqual([]);
  });

  test('no control is rendered in two places at once (v2.13.0 / C0)', async ({ page }) => {
    await page.goto('/');
    // The scale segmented control used to be painted by BOTH the inspector and
    // the tool palette simultaneously, and Fit by both header and palette.
    await expect(page.locator('button', { hasText: /^[DWMQY]$/ })).toHaveCount(5);
    await expect(page.locator('button[title*="Fit" i]')).toHaveCount(1);
    // The retired sections must not come back.
    await expect(page.locator('.inspector')).toHaveCount(0);
  });

  test('status bar carries the statistics the inspector used to show', async ({ page }) => {
    await page.goto('/');
    const status = page.locator('.app-status');
    await expect(status).toContainText('33 tasks');
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
    await expect(page.locator('.app-status')).toContainText('8 milestones');
  });
});

/**
 * Enter the command bar's document context.
 *
 * These settings have moved twice: inspector section -> File menu dialog (C0b)
 * -> command bar gear (C1f). The gear replaced the dialog rather than joining
 * it; two homes for one setting is the duplication Phase 16 exists to remove.
 */
async function openDocumentSettings(page: import('@playwright/test').Page) {
  await page.locator('.cmdbar button[aria-label="Document settings"]').click();
  // Named: the document context now holds two radio groups, Theme and
  // Overlapping bars (SRS-ELEM-050).
  await page.locator('.cmdbar [role="radiogroup"][aria-label="Theme"]').waitFor();
}
