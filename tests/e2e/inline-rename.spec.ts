import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-050/051/052/053/054 — typing a name shows the name, and a label
 * stays where it is put.
 *
 * Five symptoms from one session on the shipped v2.21.0 build, which turned
 * out to be four defects:
 *
 *  - `InlineLabel` defaulted to `color: 'white'` over a field that is
 *    `rgba(255,255,255,0.92)` in three of the four themes, so the name was
 *    invisible until Enter ("I type and I don't see at all what I'm typing.
 *    Then I press enter, then it appears").
 *  - The row gutter kept drawing the old name under that 0.92-alpha field, so
 *    the two names overlapped.
 *  - A milestone had no double-click handler at all and could only be renamed
 *    through the dialog.
 *  - A mousedown on a milestone label started a label drag whose release
 *    committed an offset however far the pointer had travelled, so a click
 *    pinned the label and threw it half its own width to the right ("it
 *    teleports away") — and selected nothing.
 *
 * Observed red on 79ccbdb. Written with iteration v2.24.0; not executed in it,
 * because the owner deferred the suite to after the release.
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

async function setTheme(page: Page, theme: 'light' | 'dark' | 'print') {
  await page.evaluate((t) => document.documentElement.setAttribute('data-theme', t), theme);
}

/** The editor's own colours, resolved by the browser rather than guessed. */
async function editorPaint(page: Page) {
  return page.locator('.inline-editor').evaluate((el) => {
    const s = getComputedStyle(el);
    return { color: s.color, caret: s.caretColor, background: s.backgroundColor };
  });
}

test.describe('renaming in place', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  for (const theme of ['light', 'dark', 'print'] as const) {
    test(`the text you type is not the colour of the field it is in (${theme})`, async ({ page }) => {
      await ready(page);
      await setTheme(page, theme);
      await page.locator('[data-pp-kind="row"]').first().dblclick();

      const paint = await editorPaint(page);
      expect(paint.color).not.toBe('rgb(255, 255, 255)');
      expect(paint.caret).toBe(paint.color);
      expect(paint.color).not.toBe(paint.background);
    });
  }

  test('what you type is what the field holds, before Enter', async ({ page }) => {
    await ready(page);
    await page.locator('[data-pp-kind="row"]').first().dblclick();
    const field = page.locator('.inline-editor');
    await field.fill('Vehicle integration');
    // The value is readable from the field while the editor is still open —
    // the defect was that it could not be read from the SCREEN.
    await expect(field).toHaveValue('Vehicle integration');
    await field.press('Enter');
    await expect(page.locator('.inline-editor')).toHaveCount(0);
  });

  test('the old name is not drawn under the open editor', async ({ page }) => {
    await ready(page);
    const row = page.locator('[data-pp-kind="row"]').first();
    const was = (await row.locator('text').first().textContent()) ?? '';
    expect(was).not.toBe('');

    await row.dblclick();
    const stillDrawn = await row.locator('text', { hasText: was }).count();
    expect(stillDrawn).toBe(0);
  });

  test('a milestone renames from a double-click on its label', async ({ page }) => {
    await ready(page);
    await page.locator('[data-pp-kind="label"][data-pp-owner="milestone"]').first().dblclick();
    await expect(page.locator('.inline-editor')).toHaveCount(1);

    await page.locator('.inline-editor').fill('Gate 3 passed');
    await page.locator('.inline-editor').press('Enter');
    await expect(page.locator('[data-pp-kind="label"]', { hasText: 'Gate 3 passed' })).toHaveCount(1);
  });
});

test.describe('a label stays where it is put', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  test('a click that never moved does not move the label', async ({ page }) => {
    await ready(page);
    const label = page.locator('[data-pp-kind="label"][data-pp-owner="milestone"]').first();
    const before = await label.boundingBox();

    await label.click();

    const after = await label.boundingBox();
    expect(after?.x).toBeCloseTo(before?.x ?? 0, 0);
    expect(after?.y).toBeCloseTo(before?.y ?? 0, 0);
  });

  test('clicking a label selects the milestone it names', async ({ page }) => {
    await ready(page);
    const label = page.locator('[data-pp-kind="label"][data-pp-owner="milestone"]').first();
    const id = await label.getAttribute('data-pp-id');

    await label.click();

    // Read from the chart, not from the store: the defect was that the chrome
    // and the footer could disagree (SRS-EDIT-023).
    await expect(page.locator(`[data-pp-kind="milestone"][data-pp-id="${id}"]`)).toHaveAttribute(
      'stroke-width',
      '2',
    );
    await expect(page.locator('.app-status')).toContainText('1 selected');
  });

  test('a drag that moves does pin it', async ({ page }) => {
    await ready(page);
    const label = page.locator('[data-pp-kind="label"][data-pp-owner="milestone"]').first();
    const box = (await label.boundingBox())!;

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 40, box.y + box.height / 2 + 10, { steps: 6 });
    await page.mouse.up();

    const after = (await label.boundingBox())!;
    expect(after.x).toBeGreaterThan(box.x + 20);
  });
});
