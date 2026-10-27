import { test, expect } from '@playwright/test';

/**
 * The division of labour between the command bar and the details dialog
 * (PLAN.md v2.20.0).
 *
 * The bar holds what reduces to one press. Everything with a field in it is in
 * the dialog, reachable from the bar's one door, from right-click, and from
 * Enter. Nothing may be reachable from neither — that is the capability gate
 * C2 set, and this replaces it.
 */
test.describe('selection editing: quick actions in the bar, fields in the dialog', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    await page.locator('[data-pp-kind="task"]').first().click();
  });

  test('the bar names the selection but does not rename it', async ({ page }) => {
    // Renaming belongs on the name, in the row list — the bar is for quick
    // actions (owner directive 2026-10-26).
    await expect(page.locator('.cmdbar .cmdbar__selname')).toBeVisible();
    await expect(page.locator('.cmdbar').getByLabel('Label')).toHaveCount(0);
  });

  test('a row is renamed by double-clicking its name in the row list', async ({ page }) => {
    // Address the ROW, not the glyphs: renaming triggers from anywhere on the
    // row, and on a narrow gutter the row's own background is the hit target.
    await page.locator('[data-pp-kind="row"][data-pp-id="row-mcal"]').dblclick();
    const editor = page.locator('.chart-area input, .chart-area textarea').first();
    await expect(editor).toBeVisible();
  });

  test('dates and percent have precise nudges', async ({ page }) => {
    const bar = page.locator('.cmdbar');
    await expect(bar.getByLabel('Move a day later')).toBeVisible();
    await expect(bar.getByLabel('Ten percent more complete')).toBeVisible();
    await bar.getByLabel('Ten percent more complete').click();
    // The dialog reports the value, so read it back there.
    await bar.getByLabel('Task details').click();
    await expect(page.locator('[role="dialog"]')).toContainText('%');
  });

  test('the bar holds no text field, date picker or dropdown', async ({ page }) => {
    // The owner's rule, stated three times: the bottom toolbar is not an
    // editing surface. An input here means something with a field in it has
    // crept back.
    const bar = page.locator('.cmdbar');
    await expect(
      bar.locator('input[type="text"], input[type="date"], textarea, select'),
    ).toHaveCount(0);
  });

  test('right-click opens the details, and never sends the user to the bar', async ({ page }) => {
    // The menu used to carry "Select, to edit in the bar" — an instruction to
    // go and use the surface the owner had already rejected.
    await page.locator('[data-pp-kind="task"]').first().click({ button: 'right' });
    const menu = page.locator('[role="menu"]');
    await expect(menu).toBeVisible();
    await expect(menu).not.toContainText('edit in the bar');

    await menu.getByText('Details…', { exact: true }).click();
    const pop = page.locator('[role="dialog"]');
    await expect(pop).toBeVisible();
    await expect(pop).toContainText('Name');
  });

  test('a row is renamed without a browser prompt', async ({ page }) => {
    // window.prompt blocks the page and cannot be styled, undone or escaped
    // into the rest of the editor. Playwright auto-dismisses it, so a prompt
    // would leave the name unchanged and this would fail.
    let prompted = false;
    page.on('dialog', (d) => { prompted = true; void d.dismiss(); });

    await page.locator('[data-pp-kind="row"][data-pp-id="row-mcal"]').click({ button: 'right' });
    await page.locator('[role="menu"]').getByText('Rename row…', { exact: true }).click();

    const pop = page.locator('[role="dialog"]');
    await expect(pop).toBeVisible();
    await pop.locator('input').first().fill('MCAL, renamed');
    await expect(page.locator('[data-pp-kind="row"][data-pp-id="row-mcal"]')).toContainText('MCAL, renamed');
    expect(prompted).toBe(false);
  });

  test('name, row, dates, percent, notes and label position live in the dialog', async ({ page }) => {
    await page.locator('.cmdbar').getByLabel('Task details').click();
    const pop = page.locator('[role="dialog"]');
    await expect(pop).toContainText('Name');
    await expect(pop).toContainText('Row');
    await expect(pop).toContainText('Start');
    await expect(pop).toContainText('End');
    await expect(pop).toContainText('Notes');
    await expect(pop.getByRole('radiogroup', { name: 'Label position' })).toBeVisible();

    await pop.locator('textarea').fill('A note written in the details dialog');
    await expect(pop.locator('textarea')).toHaveValue('A note written in the details dialog');
  });

  test('a task is renamed in the dialog, and the rest of the app follows', async ({ page }) => {
    await page.locator('.cmdbar').getByLabel('Task details').click();
    await page.locator('[role="dialog"] input').first().fill('Renamed in the dialog');
    await expect(page.locator('.cmdbar .cmdbar__selname')).toHaveText('Renamed in the dialog');
  });

  test('Enter opens the details dialog for whatever is selected', async ({ page }) => {
    await page.locator('.chart-area svg').first().press('Enter');
    await expect(page.locator('[role="dialog"]')).toBeVisible();
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
