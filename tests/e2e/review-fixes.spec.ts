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
    // The expected name came from the command bar, which no longer restates it
    // (SRS-EDIT-029). Asserted against the heading in full instead, which is
    // stronger than the substring it replaces: the defect was Row details
    // opening Task details, so the KIND matters as much as the subject.
    await page.locator('.task-bar-body[data-pp-id="task-stkreq"]').click();
    await page.locator('.cmdbar').getByLabel('Task details').click();
    await expect(page.locator('[role="dialog"] h2')).toHaveText('Task — Stakeholder requirements');
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
  // This IS a regression gate now, and the comment it replaces admitted it was
  // not: v2.26.0 gave every bar a 10px progress handle that never joined the
  // 8px body budget the resize grips observe, so on the 15.17px bar the
  // command bar inserts at the opening zoom it covered every pixel of body the
  // grips had left — and it carried no rename handler, so the double-click the
  // UI teaches died on it (SRS-EDIT-078).
  test('a one-week task can be double-clicked to rename it', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    await page.locator('.cmdbar').getByTitle('Add task (T)').click();
    const bar = page.locator('.task-bar-body').last();
    // `page.mouse` has no auto-scroll, unlike `locator.click()`. At phone
    // width the app shell is 702px wide inside a 390px viewport, so an
    // inserted task lands past the right edge and a raw press hits nothing.
    await bar.scrollIntoViewIfNeeded();
    const box = (await bar.boundingBox())!;
    expect(box.width).toBeLessThan(30);

    // Real input at the point a hand goes, not a locator-targeted click: on a
    // bar this narrow the handle genuinely covers the centre, so Playwright's
    // actionability check would refuse to address the body rect there. That
    // refusal is not the requirement — a planner double-clicks a place, and
    // whatever is under the pointer has to carry the verb (SRS-EDIT-078).
    await page.mouse.dblclick(box.x + box.width / 2, box.y + box.height / 2);
    await expect(page.locator('.chart-area input, .chart-area textarea').first()).toBeVisible();
  });

  test('the progress handle does not swallow the rename', async ({ page, viewport }) => {
    // Desktop only, because the bar has to be wide enough to CARRY a handle
    // for this to mean anything: `gripW = min(GRIP_W, floor(width - 8))`, so
    // on the narrower tablet and phone plots the inserted task falls under 8px
    // and correctly gets no grips and no handle at all (SRS-EDIT-070). Scoped
    // rather than guarded — an `if (handle exists)` here would be the vacuous
    // shape this suite has already shipped twice.
    test.skip((viewport?.width ?? 1280) < 1000, 'the bar must be wide enough to carry a handle');
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    await page.locator('.cmdbar').getByTitle('Add task (T)').click();
    const bar = page.locator('.task-bar-body').last();
    const id = (await bar.getAttribute('data-pp-id'))!;

    // The handle must be there — an inserted task arrives selected, which is
    // what summons it. Without this, deleting the handle on short bars would
    // turn the test green while taking the progress verb away, and with the
    // steppers gone (SRS-EDIT-075) that would leave progress with no gesture.
    const handle = page.locator(`[data-pp-kind="task"][data-pp-id="${id}"][data-pp-sub="progress"]`);
    await expect(handle).toHaveCount(1);

    // And a double-click ON the handle renames, which forbids the other cheap
    // fix — nudging the handle just off the geometric centre so a centre-click
    // gets through while a planner aiming at the handle still cannot rename.
    await handle.dblclick();
    await expect(page.locator('.chart-area input, .chart-area textarea').first()).toBeVisible();
  });
});

test.describe('the command palette follows the same rules as the File menu', () => {
  test('its Open asks before discarding unsaved work', async ({ page }) => {
    await page.goto('/');
    await page.locator('.chart-area svg').first().waitFor({ state: 'visible' });

    // Make it dirty, and count what is there. The +10% stepper this used to
    // press is gone (SRS-EDIT-075); inserting a row is a one-press edit that
    // needs no selection and dirties the document just as well.
    const before = await page.locator('[data-pp-kind="task"]').count();
    await page.locator('.cmdbar').getByTitle('Add row').click();

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
