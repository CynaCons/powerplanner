import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-026/027/028 — one insert is one insert, it says so while it is
 * armed, and it lands where the planner is looking.
 *
 * `setTool` defaulted `sticky` to true and every caller took the default, so
 * `consumeOneShot` never reverted anything: arming task insertion armed it
 * permanently. Three clicks meant to deselect produced three tasks, and the
 * identical branch produced two milestones. Nothing on screen said the mode was
 * on — the tool row is replaced by the selection group the moment the inserted
 * item is selected, leaving only a crosshair.
 *
 * Separately, the insert buttons and the insert keys passed `doc.rows[0]`,
 * which in every shipped template is a section header and is usually
 * off-screen, so an insert appeared to do nothing or to hang work off a
 * heading.
 *
 * Observed red on 79ccbdb: three background clicks after `t` produced 3 tasks,
 * no element named the armed tool, and a task inserted with a row selected
 * landed on the first section row instead.
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

const taskCount = (page: Page) => page.locator('.task-bar-body').count();

/** A point in the plot that is on no bar, no milestone and no marker. */
async function emptySpot(page: Page, nth: number) {
  const svg = (await page.locator('.chart-area > svg').first().boundingBox())!;
  return { x: svg.x + 240 + nth * 70, y: svg.y + svg.height - 70 };
}

test.describe('an armed insert tool acts once', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  test('three clicks after arming make one task, not three', async ({ page }) => {
    await ready(page);
    const before = await taskCount(page);

    await page.locator('.chart-area > svg').first().click({ position: { x: 400, y: 20 } });
    await page.keyboard.press('Escape');
    await page.keyboard.press('t');

    for (let i = 0; i < 3; i++) {
      const p = await emptySpot(page, i);
      await page.mouse.click(p.x, p.y);
    }

    expect(await taskCount(page)).toBe(before + 1);
  });

  test('the command bar names the armed tool, and stops when it reverts', async ({ page }) => {
    await ready(page);
    await page.keyboard.press('Escape');
    await page.keyboard.press('t');

    const chip = page.locator('[data-pp-armed]');
    await expect(chip).toBeVisible();
    await expect(chip).toHaveText(/task/i);

    const p = await emptySpot(page, 0);
    await page.mouse.click(p.x, p.y);
    await expect(chip).toHaveCount(0);
  });

  test('Escape disarms it without inserting anything', async ({ page }) => {
    await ready(page);
    const before = await taskCount(page);
    await page.keyboard.press('t');
    await page.keyboard.press('Escape');

    const p = await emptySpot(page, 0);
    await page.mouse.click(p.x, p.y);
    expect(await taskCount(page)).toBe(before);
  });
});

test.describe('an insert lands on the selected row', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  test('inserting with a task row selected puts the new task on that row', async ({ page }) => {
    await ready(page);

    // Select a task, which selects its row context.
    const anchor = page.locator('.task-bar-body').nth(4);
    const anchorId = await anchor.getAttribute('data-pp-id');
    await anchor.click();

    const targetRow = await page.evaluate((id) => {
      const el = document.querySelector(`[data-pp-id="${id}"]`);
      return el?.getAttribute('data-pp-row') ?? null;
    }, anchorId);

    await page.getByRole('button', { name: /^Task$/ }).click();

    const landedRow = await page.evaluate(() => {
      const sel = document.querySelector('[data-pp-selected="1"]');
      return sel?.getAttribute('data-pp-row') ?? null;
    });

    expect(targetRow, 'the anchor bar reports its row').toBeTruthy();
    expect(landedRow).toBe(targetRow);
  });
});
