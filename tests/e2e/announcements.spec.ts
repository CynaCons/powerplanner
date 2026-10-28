import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-PERS-025 / SRS-EDIT-019 — the app says what it did.
 *
 * There was no sink at all: the whole feedback vocabulary was two
 * `window.confirm`s and one `alert` unreachable in Chrome. A save that reached
 * the chosen file, a save that fell back to the downloads folder, a save that
 * threw, an open rejected as not a roadmap, every export and every delete were
 * one silence — and `tests/e2e/agent-bridge.spec.ts` asserted that silence,
 * with `expect(getByRole('status')).toHaveCount(0)` in the running app.
 *
 * Observed red on 0801c19: every assertion below failed on a missing element,
 * because `getByRole('status')` resolved to nothing with no agent attached.
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

/** The live region, which is the one place an outcome is announced. */
function notice(page: Page) {
  return page.locator('.app-status [role="status"]');
}

async function ready(page: Page) {
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

test.describe('outcomes are announced (SRS-PERS-025)', () => {
  test.skip(DESKTOP_ONLY, 'the File menu does not fit a phone yet');

  test('an HTML file with no roadmap in it is refused out loud', async ({ page }) => {
    await page.addInitScript(() => {
      (window as unknown as { showOpenFilePicker: unknown }).showOpenFilePicker = async () => [
        {
          name: 'notes.html',
          getFile: async () => new File(['<html><body>notes</body></html>'], 'notes.html', { type: 'text/html' }),
        },
      ];
    });
    await page.goto('/');
    await ready(page);

    await page.getByTitle('File menu').click();
    await page.getByRole('button', { name: /Open file/ }).click();

    await expect(notice(page)).toContainText(/not a PowerRoadmap file/i);
    // And the chart it refused to replace is still there.
    expect(await page.locator('[data-pp-kind="task"]').count()).toBeGreaterThan(10);
  });

  test('a cancelled save picker says the file went to Downloads instead', async ({ page }) => {
    await page.addInitScript(() => {
      (window as unknown as { showSaveFilePicker: unknown }).showSaveFilePicker = async () => {
        throw new DOMException('cancelled', 'AbortError');
      };
    });
    await page.goto('/');
    await ready(page);

    await page.keyboard.press('ControlOrMeta+s');
    await expect(notice(page)).toContainText(/download/i);
  });

  test('a save that cannot be written says so, and does not claim success', async ({ page }) => {
    await page.addInitScript(() => {
      (window as unknown as { showSaveFilePicker: unknown }).showSaveFilePicker = async () => ({
        name: 'plan.html',
        createWritable: async () => {
          throw new DOMException('disk is full', 'NotAllowedError');
        },
        getFile: async () => new File([''], 'plan.html'),
      });
    });
    await page.goto('/');
    await ready(page);

    await page.keyboard.press('ControlOrMeta+s');
    await expect(notice(page)).toContainText(/could not be saved/i);
  });
});

test.describe('destructive edits are announced (SRS-EDIT-019)', () => {
  test.skip(DESKTOP_ONLY, 'the chart needs a desktop window');

  test('a multi-item delete says how many it took', async ({ page }) => {
    await page.goto('/');
    await ready(page);

    const bars = page.locator('.task-bar-body');
    await bars.nth(0).click();
    await bars.nth(1).click({ modifiers: ['Shift'] });
    await bars.nth(2).click({ modifiers: ['Shift'] });
    await expect(page.locator('.app-status')).toContainText('3 selected');

    await page.keyboard.press('Delete');
    await expect(notice(page)).toContainText('3');
  });

  test('a single delete names what went', async ({ page }) => {
    await page.goto('/');
    await ready(page);

    await page.locator('.task-bar-body').first().click();
    await page.keyboard.press('Delete');
    await expect(notice(page)).toContainText(/deleted/i);
  });
});
