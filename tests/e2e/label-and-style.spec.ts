import { test, expect, type Page } from '@playwright/test';

/**
 * SRS-EDIT-083, SRS-ELEM-072, SRS-ELEM-073 — owner requests of 2026-10-30.
 *
 * "Sometimes the label for a task or a milestone is on the side ... I
 * double-clicked on the label, and it did not enter edit mode. I had to
 * double-click on the task where the label was not." Plus a colour for the
 * label text, and an outline for the bar: none, thin, or dashed.
 */

const ready = async (page: Page) => {
  await page.goto('/');
  await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
};

test.describe('renaming from the label (SRS-EDIT-083)', () => {
  test.skip(({ viewport }) => (viewport?.width ?? 0) < 900, 'the labels need a desktop window');

  test('a task label beside its bar opens the rename', async ({ page }) => {
    await ready(page);
    const label = page.locator('text[data-pp-kind="label"][data-pp-owner="task"]').first();
    await expect(label, 'a task label that is not on the bar').toBeVisible();

    await label.dblclick();
    // The same editor the bar's own double-click opens.
    await expect(page.locator('.chart-area input, .chart-area [contenteditable]')).toHaveCount(1);
  });

  test('a label ON the bar stays out of the way of dragging it', async ({ page }) => {
    await ready(page);
    // An on-bar label sits over the body and outranks it, so making it
    // clickable would send the press that moves a bar to its own name.
    const onBar = await page.evaluate(() => {
      const labels = [...document.querySelectorAll('text[data-pp-owner="task"]')];
      return labels.filter((l) => {
        const r = l.getBoundingClientRect();
        const bar = document.querySelector(`.task-bar-body[data-pp-id="${(l as HTMLElement).dataset.ppId}"]`);
        if (!bar) return false;
        const b = bar.getBoundingClientRect();
        return r.left >= b.left && r.right <= b.right;
      }).length;
    });
    expect(onBar, 'no on-bar label is hit-testable').toBe(0);
  });
});

test.describe('how a bar is drawn (SRS-ELEM-072, SRS-ELEM-073)', () => {
  test.skip(({ viewport }) => (viewport?.width ?? 0) < 900, 'the panel needs a desktop window');

  test('a bar takes no outline, a thin one, or a dashed one', async ({ page }) => {
    await ready(page);
    const bar = page.locator('.chart-area svg .task-bar-body').first();
    await bar.click();
    const id = await bar.getAttribute('data-pp-id');
    const outline = page.locator('[aria-label="Outline"]');
    await expect(outline).toBeVisible();

    const strokeOf = () =>
      page.evaluate((taskId) => {
        const g = document.querySelector(`.task-bar-body[data-pp-id="${taskId}"]`)?.parentElement;
        const o = g?.querySelector('.bar-outline');
        return o
          ? { stroke: o.getAttribute('stroke'), dash: o.getAttribute('stroke-dasharray') }
          : null;
      }, id);

    expect(await strokeOf(), 'no outline until one is asked for').toBeNull();

    await outline.getByRole('radio', { name: 'Thin' }).click();
    const thin = await strokeOf();
    expect(thin).not.toBeNull();
    expect(thin!.dash, 'thin is a solid line').toBeNull();

    await outline.getByRole('radio', { name: 'Dashed' }).click();
    expect((await strokeOf())!.dash).toBeTruthy();

    // A colour of its own, once there is a line to colour. Scoped by group:
    // the panel now carries three palettes and every one has a Rust.
    await page.locator('[data-pp-section="selection"]').getByLabel('Rust outline').click();
    await expect
      .poll(async () => (await strokeOf())!.stroke?.toLowerCase())
      .toBe('#a8452b');

    await outline.getByRole('radio', { name: 'None' }).click();
    expect(await strokeOf()).toBeNull();
  });

  test('the label takes the colour it is given, and keeps it through a save', async ({ page, context }) => {
    await ready(page);
    await page.addInitScript(() => {
      delete (window as unknown as { showSaveFilePicker?: unknown }).showSaveFilePicker;
    });
    const bar = page.locator('.chart-area svg .task-bar-body').first();
    await bar.click();
    const id = await bar.getAttribute('data-pp-id');

    const labelFill = () =>
      page.evaluate(
        (taskId) =>
          document.querySelector(`text[data-pp-kind="label"][data-pp-id="${taskId}"]`)?.getAttribute('fill')
          ?? document.querySelector(`.task-bar-body[data-pp-id="${taskId}"]`)?.parentElement?.querySelector('text')?.getAttribute('fill')
          ?? null,
        id,
      );

    await page.locator('[data-pp-section="selection"]').getByLabel('Rust label').click();
    await expect.poll(labelFill).toMatch(/#a8452b/i);

    // SRS-PERS-033 again, for fields that did not exist when it was written.
    // The snapshot is debounced at 800ms; reloading before it lands would be
    // measuring the sample, not the round trip.
    await page.waitForTimeout(1500);
    await page.reload();
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    const banner = page.getByRole('button', { name: 'Restore' });
    if (await banner.count()) await banner.click();
    await expect.poll(labelFill).toMatch(/#a8452b/i);
    expect(context).toBeDefined();
  });
});
