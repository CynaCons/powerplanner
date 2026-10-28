import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-022 — one gesture, one undo step.
 *
 * `mutate` pushed a history frame on every single call, and the store had no
 * notion of a gesture, so every controlled input committed one frame per
 * keystroke and every multi-item command looped per-item setters. Undo is this
 * app's only recovery path and it was being spent on characters: a 12-character
 * rename cost 12 frames of a 100-frame history, and a 120-character note
 * evicted the whole session.
 *
 * Observed red on e221b5f:
 *   rename  one Ctrl+Z left "Stakeholder requirementsX" — the 11-char prefix
 *           of the new name — instead of the original
 *   delete  one Ctrl+Z brought back 1 of 3 bars
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

function framesOf(page: Page) {
  return page.evaluate(() => document.querySelectorAll('.task-bar-body').length);
}

test.describe('undo granularity (SRS-EDIT-022)', () => {
  test.skip(DESKTOP_ONLY, 'the details dialog needs a desktop window');

  test('one Ctrl+Z takes back a whole rename, not its last letter', async ({ page }) => {
    await ready(page);

    const bar = page.locator('.task-bar-body').first();
    const id = await bar.getAttribute('data-pp-id');
    /** The task's name as the app holds it, read back through the dialog. */
    const labelOf = async () => {
      await page.locator(`.task-bar-body[data-pp-id="${id}"]`).click();
      await page.keyboard.press('Enter');
      const field = page.getByRole('dialog').locator('input.dialog__input').first();
      const value = await field.inputValue();
      await page.getByRole('dialog').getByRole('button', { name: /close/i }).click();
      await expect(page.getByRole('dialog')).toHaveCount(0);
      return value;
    };

    await bar.click();
    await page.keyboard.press('Enter');
    const name = page.getByRole('dialog').locator('input.dialog__input').first();
    await expect(name).toBeVisible();
    const original = await name.inputValue();

    await name.press('ControlOrMeta+a');
    await name.type('Requirements sign-off');
    await expect(name).toHaveValue('Requirements sign-off');

    await page.getByRole('dialog').getByRole('button', { name: /close/i }).click();
    await expect(page.getByRole('dialog')).toHaveCount(0);

    await page.keyboard.press('ControlOrMeta+z');
    expect(await labelOf()).toBe(original);
  });

  test('one Ctrl+Z brings back a whole multi-item delete', async ({ page }) => {
    await ready(page);

    const before = await framesOf(page);
    const bars = page.locator('.task-bar-body');
    await bars.nth(0).click();
    await bars.nth(1).click({ modifiers: ['Shift'] });
    await bars.nth(2).click({ modifiers: ['Shift'] });
    await expect(page.locator('.app-status')).toContainText('3 selected');

    await page.keyboard.press('Delete');
    expect(await framesOf(page)).toBe(before - 3);

    await page.keyboard.press('ControlOrMeta+z');
    expect(await framesOf(page)).toBe(before);
  });

  test('one Ctrl+Z takes back a whole slider drag', async ({ page }) => {
    await ready(page);

    await page.locator('.task-bar-body').first().click();
    await page.keyboard.press('Enter');
    const slider = page.getByRole('dialog').locator('input[type="range"]');
    const before = await slider.inputValue();

    // Each arrow press is one step the thumb crosses, which is exactly what a
    // drag produces — one store write per integer. Direction follows the
    // starting value, since the sample's first task is already at 100%.
    const key = Number(before) > 50 ? 'ArrowLeft' : 'ArrowRight';
    await slider.focus();
    for (let i = 0; i < 8; i++) await slider.press(key);
    await expect(slider).not.toHaveValue(before);

    await page.getByRole('dialog').getByRole('button', { name: /close/i }).click();
    await page.keyboard.press('ControlOrMeta+z');

    await page.locator('.task-bar-body').first().click();
    await page.keyboard.press('Enter');
    await expect(page.getByRole('dialog').locator('input[type="range"]')).toHaveValue(before);
  });

  test('a held nudge key is one step to undo, not one per repeat', async ({ page }) => {
    await ready(page);

    const bar = page.locator('.task-bar-body').first();
    await bar.click();
    const startX = (await bar.boundingBox())!.x;

    for (let i = 0; i < 6; i++) await page.keyboard.press('ArrowRight');
    await expect.poll(async () => (await page.locator('.task-bar-body').first().boundingBox())!.x).not.toBe(startX);

    await page.keyboard.press('ControlOrMeta+z');
    await expect
      .poll(async () => Math.round((await page.locator('.task-bar-body').first().boundingBox())!.x))
      .toBe(Math.round(startX));
  });
});
