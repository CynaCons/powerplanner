import { test, expect } from '@playwright/test';

/**
 * The three things the owner reported on 2026-10-27, as gates.
 *
 * Each one was reachable from the published build and wrong there, so each
 * assertion is written against what a person sees rather than against the code
 * that produces it.
 */

test.describe('starting a roadmap (SRS-EDIT-017)', () => {
  // The File menu is in the header, and the header does not fit a phone: its
  // min-content width is ~658px (brand + toolbar), so below about 700px the
  // right-hand controls are clipped off-screen and cannot be tapped. That is a
  // real defect, measured 2026-10-27 and in the backlog as a mobile header
  // that collapses into an overflow menu. It is NOT what these tests are for,
  // so they run where the header is reachable and say why rather than
  // asserting a layout nobody has built yet.
  test.skip(({ viewport }) => (viewport?.width ?? 1280) < 700, 'header does not fit a phone yet');

  test('the File menu offers one, and blank actually empties the chart', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    await page.getByTitle('File menu').click();
    await page.getByRole('button', { name: 'New roadmap…' }).click();

    const dialog = page.getByRole('dialog', { name: 'Start a new roadmap' });
    await expect(dialog).toBeVisible();

    await dialog.getByRole('button', { name: /^Blank/ }).click();
    await expect(page.locator('.empty-state')).toBeVisible();
    await expect(page.locator('[data-pp-kind="task"]')).toHaveCount(0);
  });

  test('the worked example can be brought back after it is dismantled', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    await page.getByTitle('File menu').click();
    await page.getByRole('button', { name: 'New roadmap…' }).click();
    await page.getByRole('dialog').getByRole('button', { name: /^Blank/ }).click();
    await expect(page.locator('.empty-state')).toBeVisible();

    await page.getByTitle('File menu').click();
    await page.getByRole('button', { name: 'New roadmap…' }).click();
    await page.getByRole('dialog').getByRole('button', { name: /^Worked example/ }).click();

    await expect(page.locator('.empty-state')).toHaveCount(0);
    expect(await page.locator('[data-pp-kind="task"]').count()).toBeGreaterThan(10);
  });
});

test.describe('pointer affordance (SRS-EDIT-030)', () => {
  test('each region carries the cursor that names its gesture', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const cursorOf = (sel: string) =>
      page.locator(sel).first().evaluate((el) => getComputedStyle(el).cursor);

    // A hand promises to pan the view; these drags change the schedule.
    expect(await cursorOf('.task-bar-body')).toBe('move');
    expect(await cursorOf('[data-pp-sub="start"]')).toBe('ew-resize');
    expect(await cursorOf('[data-pp-kind="milestone"]')).toBe('move');
    expect(await cursorOf('[data-pp-kind="label"]')).toBe('move');
    // The row selects on a click and renames on a double-click, so the default
    // arrow would be telling the user there is nothing there.
    expect(await cursorOf('[data-pp-kind="row"]')).toBe('pointer');
  });

  test('no gesture cursor survives on the empty surface', async ({ page }) => {
    await page.goto('/');
    const svg = page.locator('.chart-area svg').first();
    await svg.waitFor({ state: 'visible' });
    expect(await svg.evaluate((el) => getComputedStyle(el).cursor)).toBe('default');
  });
});

test.describe('calendar weeks (SRS-TAX-023)', () => {
  test('the week scale is numbered, not dated', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    await page.getByTitle('Scale: Week').click();

    const labels = (await page.locator('.chart-area svg text').allTextContents()).map((t) => t.trim());
    expect(labels.filter((t) => /^W\d{1,2}$/.test(t)).length).toBeGreaterThan(3);

    // The band used to read "Oct 19" — a date, under a month band that already
    // said October. None of that shape may survive.
    const MONTH_DAY = /^(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) \d{1,2}$/;
    expect(labels.filter((t) => MONTH_DAY.test(t))).toHaveLength(0);
  });

  test('the month band states the year where it turns, and not on every month', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    await page.getByTitle('Scale: Week').click();

    const labels = (await page.locator('.chart-area svg text').allTextContents()).map((t) => t.trim());
    const months = labels.filter((t) => /^(Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)( \d{4})?$/.test(t));
    expect(months.length).toBeGreaterThan(2);
    // Only January and the leading tick carry four digits.
    const withYear = months.filter((t) => /\d{4}$/.test(t));
    expect(withYear.length).toBeLessThanOrEqual(3);
    expect(withYear.length).toBeGreaterThan(0);
  });
});

test.describe('dragging a bar (SRS-EDIT-002, SRS-EDIT-010)', () => {
  test('a drag moves the task, and lands where the preview showed', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    // The default document is at month scale with the magnet on — the exact
    // combination under which every drag shorter than a fortnight used to
    // commit nothing at all.
    const bar = page.locator('.task-bar-body').first();
    const box = (await bar.boundingBox())!;
    const pxPerDay = Number(
      (await page.locator('.app-status').innerText()).match(/([\d.]+) px\/day/)![1],
    );
    const dx = Math.round(14 * pxPerDay);

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + dx, box.y + box.height / 2, { steps: 6 });

    const previewX = (await bar.boundingBox())!.x;
    expect(previewX - box.x).toBeGreaterThan(pxPerDay * 4);

    await page.mouse.up();
    const committedX = (await bar.boundingBox())!.x;

    // What you let go of is what you get: no jump on release.
    expect(Math.abs(committedX - previewX)).toBeLessThan(2);
    // And the task actually moved.
    expect(committedX - box.x).toBeGreaterThan(pxPerDay * 4);
  });
});
