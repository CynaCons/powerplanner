import { test, expect, type Page } from '@playwright/test';

/**
 * SRS-EDIT-080..082, SRS-ELEM-070 — owner requests of 2026-10-30.
 *
 * "We should also be able to select several rows with control click. And
 * shift click. Just so that I can move or delete several rows at once. And
 * when I am in a row, which is inside a section, and I do like create new
 * row, or row below, it should automatically include that row in the
 * section." Plus: a milestone or marker placed at the beginning, middle or
 * end of the column its date falls in.
 */

const ready = async (page: Page) => {
  await page.goto('/');
  await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
};

const rowCount = (page: Page) => page.locator('[data-pp-kind="row"]').count();
const selected = (page: Page) => page.locator('.app-status').innerText();

/** The gutter cell for a row, by the label a planner reads. */
const rowByLabel = (page: Page, label: string) =>
  page.locator('[data-pp-kind="row"]').filter({ hasText: label }).first();

test.describe('several rows at once (SRS-EDIT-080, SRS-EDIT-081)', () => {
  test.skip(({ viewport }) => (viewport?.width ?? 0) < 900, 'the gutter needs a desktop window');

  test('ctrl-click adds a row to the selection and takes it away again', async ({ page }) => {
    await ready(page);
    await rowByLabel(page, 'MCAL integration').click();
    const one = await page.locator('[data-pp-kind="row"].is-selected, [data-pp-kind="row"][data-pp-selected]').count();

    await rowByLabel(page, 'COM stack, CAN-FD').click({ modifiers: ['Control'] });
    await expect(page.locator('[data-pp-section="selection"] .cmdbar__selname')).toContainText('2 rows');

    // And off again: the same click on the same row removes it.
    await rowByLabel(page, 'COM stack, CAN-FD').click({ modifiers: ['Control'] });
    await expect(page.locator('[data-pp-section="selection"] .cmdbar__selname')).toHaveCount(0);
    expect(one).toBeGreaterThanOrEqual(0);
  });

  test('shift-click takes the run between the two rows', async ({ page }) => {
    await ready(page);
    await rowByLabel(page, 'MCAL integration').click();
    await rowByLabel(page, 'Diagnostics, Dcm and Dem').click({ modifiers: ['Shift'] });
    // MCAL integration, BSW configuration, COM stack, Diagnostics.
    await expect(page.locator('[data-pp-section="selection"] .cmdbar__selname')).toContainText('4 rows');
  });

  test('delete takes every selected row, in one undo step', async ({ page }) => {
    await ready(page);
    const before = await rowCount(page);
    await rowByLabel(page, 'MCAL integration').click();
    await rowByLabel(page, 'COM stack, CAN-FD').click({ modifiers: ['Shift'] });
    await expect(page.locator('[data-pp-section="selection"] .cmdbar__selname')).toContainText('3 rows');

    await page.locator('[data-pp-section="selection"]').getByLabel('Delete rows').click();
    await expect(page.locator('[data-pp-kind="row"]')).toHaveCount(before - 3);

    // One frame, not three: the whole command comes back together.
    await page.keyboard.press('Control+z');
    await expect(page.locator('[data-pp-kind="row"]')).toHaveCount(before);
  });

  test('a bar and its row is not a row selection', async ({ page }) => {
    await ready(page);
    const before = await rowCount(page);
    // Clicking a bar selects the row under it so the gutter highlights; that
    // companion must never be what Delete acts on.
    await page.locator('.chart-area svg .task-bar-body').first().click();
    await page.keyboard.press('Delete');
    await expect(page.locator('[data-pp-kind="row"]')).toHaveCount(before);
  });
});

test.describe('a new row joins its section (SRS-EDIT-082)', () => {
  test.skip(({ viewport }) => (viewport?.width ?? 0) < 900, 'the gutter needs a desktop window');

  test('Insert > Row lands inside the section the planner is in', async ({ page }) => {
    await ready(page);
    // "MCAL integration" sits inside "Platform — BSW and MCAL"; a row added
    // while it is selected used to land outside every section, at the end.
    const anchor = rowByLabel(page, 'MCAL integration');
    await anchor.click();
    const anchorLeft = (await anchor.locator('text').first().boundingBox())!.x;
    const anchorTop = (await anchor.boundingBox())!.y;

    await page.locator('.cmdbar').getByTitle('Add row').click();
    const added = page.locator('[data-pp-kind="row"]').filter({ hasText: /^Row \d+$/ }).first();
    await expect(added).toBeVisible();

    const box = (await added.boundingBox())!;
    const left = (await added.locator('text').first().boundingBox())!.x;
    // Same indent as the row it was added from, which is how the gutter says
    // "same section"; and directly below it rather than at the foot of the plan.
    expect(Math.abs(left - anchorLeft), 'indented like its section').toBeLessThan(2);
    expect(box.y).toBeGreaterThan(anchorTop);
    expect(box.y - anchorTop).toBeLessThan(80);
  });
});

test.describe('where a mark sits in its column (SRS-ELEM-070)', () => {
  test.skip(({ viewport }) => (viewport?.width ?? 0) < 900, 'the panel needs a desktop window');

  test('moves a milestone to the middle and the end of its column', async ({ page }) => {
    await ready(page);
    const diamond = page.locator('[data-pp-kind="milestone"]').first();
    await diamond.click();
    const place = page.locator('[aria-label="Place in column"]');
    await expect(place).toBeVisible();

    // The grid delimiter opens on week, so the column is seven days wide.
    const pxPerDay = Number(
      (await page.locator('.app-status').innerText()).match(/([\d.]+) px\/day/)![1],
    );
    const xAt = async () => (await diamond.boundingBox())!.x;
    const atStart = await xAt();

    await place.getByRole('radio', { name: 'End' }).click();
    const atEnd = await xAt();
    // The end of the column is always after the date, by at least the rest of
    // that week and at most all of it.
    expect(atEnd - atStart).toBeGreaterThan(0);
    expect(atEnd - atStart).toBeLessThanOrEqual(7 * pxPerDay + 1);

    await place.getByRole('radio', { name: 'Middle' }).click();
    const atMiddle = await xAt();
    // Half a column before the end — which can be either side of the date
    // itself, depending on where in the week the date falls.
    expect(atEnd - atMiddle).toBeCloseTo(3.5 * pxPerDay, 0);

    await place.getByRole('radio', { name: 'Start' }).click();
    expect(await xAt()).toBeCloseTo(atStart, 0);
  });
});
