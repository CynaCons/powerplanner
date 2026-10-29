import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-LAY-024 — the overlap control reaches the chart.
 *
 * `layoutDocument` took an `allowOverlap` argument from the day the lane
 * packer was written and no caller ever passed it, so the one thing a planner
 * would call think-cell overlap was dead code — reachable from a test and
 * from nowhere a user can click.
 *
 * Observed red on 340ca5a: there was no "Overlapping bars" control to click.
 *
 * The control's wording is itself a requirement now — SRS-LAY-025 asks each
 * choice to be named by what happens to bars that cover the same dates, which
 * is why "Overlapping bars / Stack / Overlap" became "Bars on the same dates /
 * Own line / Same line". The strings below have to track `OVERLAP` in
 * src/app/CommandBar.tsx; renaming the control without this file is what made
 * both tests here red on d101e0b.
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/**
 * How deep the plan runs, read from the bars themselves.
 *
 * The shipped sample puts milestones on their package's end date, so five rows
 * take a second lane and stand 72px rather than 36px. Four of those sit above
 * the last bar — the fifth is the Sample builds row itself, whose bars are on
 * lane 0 either way — so sharing the band takes 4 x ROW_HEIGHT out of the
 * distance between the first bar and the last.
 */
async function planDepth(page: Page) {
  return page.evaluate(() => {
    const tops = Array.from(document.querySelectorAll('.task-bar-body')).map(
      (el) => el.getBoundingClientRect().top,
    );
    return Math.round(Math.max(...tops) - Math.min(...tops));
  });
}

/** Whether two elements' vertical centres land in the same 36px band. */
async function sameBand(page: Page, taskId: string, milestoneId: string) {
  return page.evaluate(([t, m]) => {
    const mid = (sel: string) => {
      const el = document.querySelector(sel);
      if (!el) return NaN;
      const b = el.getBoundingClientRect();
      return b.top + b.height / 2;
    };
    const a = mid(`.task-bar-body[data-pp-id="${t}"]`);
    const b = mid(`[data-pp-kind="milestone"][data-pp-id="${m}"]`);
    return Number.isFinite(a) && Number.isFinite(b) && Math.abs(a - b) < 18;
  }, [taskId, milestoneId]);
}

function overlapGroup(page: Page) {
  return page.locator('.cmdbar [role="radiogroup"][aria-label="Bars on the same dates"]');
}

test.describe('overlapping bars (SRS-LAY-024)', () => {
  test.skip(DESKTOP_ONLY, 'the document settings need a desktop window');

  test('the document can be told to let bars share a band', async ({ page }) => {
    await ready(page);
    await page.locator('.cmdbar button[aria-label="Document settings"]').click();

    const group = overlapGroup(page);
    await expect(group).toBeVisible();
    await expect(group.getByRole('radio', { name: 'Own line' })).toHaveAttribute('aria-checked', 'true');

    const stacked = await planDepth(page);
    expect(await sameBand(page, 'task-swarch', 'ms-swfreeze')).toBe(false);

    await group.getByRole('radio', { name: 'Same line' }).click();
    await expect(group.getByRole('radio', { name: 'Same line' })).toHaveAttribute('aria-checked', 'true');
    expect(await planDepth(page)).toBe(stacked - 4 * 36);

    // And the thing the setting is actually for: the milestone that was
    // pushed into a second lane now sits on its task's own band.
    expect(await sameBand(page, 'task-swarch', 'ms-swfreeze')).toBe(true);

    await group.getByRole('radio', { name: 'Own line' }).click();
    expect(await planDepth(page)).toBe(stacked);
  });

  test('the choice is one undo step, like any other document setting', async ({ page }) => {
    await ready(page);
    await page.locator('.cmdbar button[aria-label="Document settings"]').click();
    const stacked = await planDepth(page);

    await overlapGroup(page).getByRole('radio', { name: 'Same line' }).click();
    expect(await planDepth(page)).not.toBe(stacked);

    await page.locator('.chart-area').click({ position: { x: 400, y: 300 } });
    await page.keyboard.press('ControlOrMeta+z');
    expect(await planDepth(page)).toBe(stacked);
  });
});
