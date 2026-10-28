import { test, expect } from '@playwright/test';

/**
 * The defects an adversarial review of 2026-10-28 confirmed in the owner-round
 * changes. Each test is the behaviour that was missing, written against what a
 * person does rather than against the code that broke.
 */

test.describe('the details dialog edits what you picked', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
  });

  test('Row details opens the ROW, not the first task sitting on it', async ({ page }) => {
    // Selecting a row selects everything on it, so a dialog that inferred its
    // subject from the selection opened Task details and autofocused the
    // task's name — typing the row name renamed a package instead.
    await page.locator('[data-pp-kind="row"][data-pp-id="row-mcal"]').click();
    await page.locator('.cmdbar').getByLabel('Row details').click();

    const dialog = page.locator('[role="dialog"]');
    await expect(dialog).toBeVisible();
    await expect(dialog.locator('h2')).toContainText('Row —');
    await expect(dialog).toContainText('Section');
  });

  test('Enter does nothing with several things selected', async ({ page }) => {
    const bars = page.locator('[data-pp-kind="task"]');
    await bars.nth(0).click();
    await bars.nth(2).click({ modifiers: ['Shift'] });
    await bars.nth(4).click({ modifiers: ['Shift'] });

    await page.locator('.chart-area svg').first().press('Enter');
    await expect(page.locator('[role="dialog"]')).toHaveCount(0);
  });

  test('deleting the subject closes the dialog instead of latching it open', async ({ page }) => {
    await page.locator('[data-pp-kind="task"]').first().click();
    await page.locator('.cmdbar').getByLabel('Task details').click();
    await expect(page.locator('[role="dialog"]')).toBeVisible();

    await page.locator('[role="dialog"] button[aria-label="Close"]').click();
    await page.locator('.cmdbar').getByLabel('Delete task').click();

    // The next ordinary click must not pop a modal nobody asked for.
    await page.locator('[data-pp-kind="task"]').first().click();
    await expect(page.locator('[role="dialog"]')).toHaveCount(0);
  });

  test('the dialog names the thing it is editing', async ({ page }) => {
    await page.locator('[data-pp-kind="task"]').first().click();
    const name = await page.locator('.cmdbar .cmdbar__selname').innerText();
    await page.locator('.cmdbar').getByLabel('Task details').click();
    await expect(page.locator('[role="dialog"] h2')).toContainText(name);
  });
});

test.describe('a modal owns the keyboard while it is open', () => {
  test.skip(({ viewport }) => (viewport?.width ?? 1280) < 700, 'header does not fit a phone yet');

  test('Enter on the focused choice starts a roadmap, not the details dialog', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    // A task selected is the normal state before deciding to start over.
    await page.locator('[data-pp-kind="task"]').first().click();

    await page.getByTitle('File menu').click();
    await page.getByRole('button', { name: 'New roadmap…' }).click();
    await expect(page.getByRole('dialog', { name: 'Start a new roadmap' })).toBeVisible();

    await page.keyboard.press('Enter');

    await expect(page.locator('.empty-state')).toBeVisible();
    await expect(page.getByRole('dialog', { name: 'Start a new roadmap' })).toHaveCount(0);
    await expect(page.locator('[role="dialog"]')).toHaveCount(0);
  });
});

test.describe('short bars stay operable', () => {
  // NOT a regression gate: this passes against the pre-fix code too. The
  // review predicted that two fixed 5px grips would cover a ~10px bar end to
  // end and swallow the double-click; measured, the inserted task is wide
  // enough that it does not. The handle clamp stays as a defensive bound and
  // this stands as an invariant test, not proof of a fix.
  test('a one-week task can be double-clicked to rename it', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    // Insert one at the shipped zoom — about 10px wide, which the two fixed
    // 5px resize grips used to cover end to end.
    await page.locator('.cmdbar').getByTitle('Add task (T)').click();
    const bar = page.locator('.task-bar-body').last();
    const box = (await bar.boundingBox())!;
    expect(box.width).toBeLessThan(30);

    await bar.dblclick();
    await expect(page.locator('.chart-area input, .chart-area textarea').first()).toBeVisible();
  });
});

test.describe('the command palette follows the same rules as the File menu', () => {
  test('its Open asks before discarding unsaved work', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    // Make it dirty, and count what is there.
    await page.locator('[data-pp-kind="task"]').first().click();
    await page.locator('.cmdbar').getByLabel('Ten percent more complete').click();
    const before = await page.locator('[data-pp-kind="task"]').count();

    // Playwright auto-dismisses dialogs, so a confirm here reads as "No".
    let asked = false;
    page.on('dialog', (d) => { asked = true; void d.dismiss(); });

    await page.keyboard.press('ControlOrMeta+k');
    const palette = page.locator('[aria-label="Command palette"]');
    await expect(palette).toBeVisible();
    await page.keyboard.type('open');
    await expect(palette.getByText('Open file…')).toBeVisible();
    await palette.getByText('Open file…').click();

    await expect.poll(() => asked).toBe(true);
    await expect(page.locator('[data-pp-kind="task"]')).toHaveCount(before);
  });
});

test.describe('the command palette owns the keyboard while it is open', () => {
  test('typing a query never reaches the global shortcuts', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    const counts = async () => {
      const t = await page.locator('.app-status').innerText();
      return {
        tasks: Number(t.match(/(\d+) tasks/)![1]),
        milestones: Number(t.match(/(\d+) milestones/)![1]),
      };
    };
    const before = await counts();

    // Every letter here is also a global shortcut: m inserts a milestone,
    // n a task, b a bracket.
    await page.keyboard.press('ControlOrMeta+k');
    await expect(page.locator('[aria-label="Command palette"]')).toBeVisible();

    // Simulate the window the deferred focus used to leave open: the palette
    // is up, but the keystrokes land on <body>. Relying on the real race makes
    // this pass on an idle machine and fail only under load, which is no gate
    // at all — so put the keys where the race used to put them.
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    await page.keyboard.type('milestone number b');

    const during = await counts();
    expect(during).toEqual(before);

    await page.keyboard.press('Escape');
    expect(await counts()).toEqual(before);
  });

  test('Ctrl+K still closes it — the palette does not swallow its own chord', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    const palette = page.locator('[aria-label="Command palette"]');

    await page.keyboard.press('ControlOrMeta+k');
    await expect(palette).toBeVisible();
    await page.keyboard.press('ControlOrMeta+k');
    await expect(palette).toHaveCount(0);
  });

  test('one Enter runs one command', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });
    const milestones = async () =>
      Number((await page.locator('.app-status').innerText()).match(/(\d+) milestones/)![1]);
    const before = await milestones();

    await page.keyboard.press('ControlOrMeta+k');
    await page.keyboard.type('milestone');
    await expect(page.locator('[aria-label="Command palette"]')).toContainText('New milestone');
    await page.keyboard.press('Enter');

    await expect(page.locator('[aria-label="Command palette"]')).toHaveCount(0);
    expect(await milestones()).toBe(before + 1);
  });
});
