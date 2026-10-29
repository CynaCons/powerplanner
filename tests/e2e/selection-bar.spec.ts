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

  test('the bar does not rename the selection', async ({ page }) => {
    // The name itself is gone from the bar (SRS-EDIT-029), and that absence is
    // gated by selection-paint.spec.ts, which asserts the inverse of what this
    // test used to. What is still live here is that no name FIELD has crept
    // back in its place — and the field to watch for is labelled 'Name', as
    // DetailsDialog calls it, not 'Label', so the original locator would have
    // missed the very thing it was written to catch.
    await expect(page.locator('.cmdbar').getByLabel('Label')).toHaveCount(0);
    await expect(page.locator('.cmdbar').getByLabel('Name')).toHaveCount(0);
  });

  test('a row is renamed by double-clicking its name in the row list', async ({ page }) => {
    // Address the ROW, not the glyphs: renaming triggers from anywhere on the
    // row, and on a narrow gutter the row's own background is the hit target.
    await page.locator('[data-pp-kind="row"][data-pp-id="row-mcal"]').dblclick();
    const editor = page.locator('.chart-area input, .chart-area textarea').first();
    await expect(editor).toBeVisible();
  });

  test('precise dates and percent live in the dialog, not in the bar', async ({ page }) => {
    const bar = page.locator('.cmdbar');
    // SRS-EDIT-075 / SR-IXC-01: no stepper for a continuous value. The bar
    // carried four — −1d, +1d, −10%, +10% — and the verbs are now dragging the
    // bar and dragging its fill edge.
    await expect(bar.getByLabel('Move a day later')).toHaveCount(0);
    await expect(bar.getByLabel('Ten percent more complete')).toHaveCount(0);

    await bar.getByLabel('Task details').click();
    const pop = page.locator('[role="dialog"]');
    // Two date fields, for the precise entry SR-IXC-02 reserves a secondary
    // editor for.
    await expect(pop.locator('input[type="date"]')).toHaveCount(2);

    // The sample's first task is at 100%, so Home drives the slider to a value
    // it did not already hold. The old assertion was `toContainText('%')`,
    // which the dialog's own "Complete — {pct}%" label satisfies whatever the
    // value is: green while checking nothing.
    const pct = pop.locator('input[type="range"]');
    await pct.focus();
    await pct.press('Home');
    await expect(pop.getByText('Complete — 0%')).toHaveCount(1);
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
    // The requirement is not retired — only its observable moved. The bar used
    // to restate the name and no longer may (SRS-EDIT-029), so the chart's own
    // label is where the propagation can be seen, which is also the surface
    // the planner is looking at.
    //
    // Asserted on the NEW name only. The row gutter draws `row.label` into the
    // same svg, and for task-stkreq that row label is the identical string —
    // so a filter on the old name matches two nodes, and the row's never
    // follows a task rename. Pinned to task-stkreq because it is alone on its
    // row, so `fitLabel` returns the full text rather than hiding it.
    // `toHaveCount`, never `toBeVisible`: the task layer is clipped.
    const chartText = page.locator('.chart-area svg text');
    await expect(chartText.filter({ hasText: 'Renamed in the dialog' })).toHaveCount(0);

    await page.locator('.task-bar-body[data-pp-id="task-stkreq"]').click();
    await page.locator('.cmdbar').getByLabel('Task details').click();
    await page.locator('[role="dialog"] input').first().fill('Renamed in the dialog');
    await page.locator('[role="dialog"] button[aria-label="Close"]').click();

    // 0 before and exactly 1 after is the whole anti-vacuity burden: the first
    // proves the locator was not already satisfied, the second that the label
    // node exists and repainted.
    await expect(chartText.filter({ hasText: 'Renamed in the dialog' })).toHaveCount(1);
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
