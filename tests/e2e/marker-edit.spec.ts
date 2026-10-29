import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-ELEM-051/052/053 — a deadline marker can be reached, moved and renamed.
 *
 * Owner report 2026-10-29, on the shipped v2.21.0 build: "Once I have created
 * a deadline marker, I cannot edit the text, I cannot move it around." He was
 * right in the strongest sense — `Markers` drew a line and a text node with no
 * hit target of any kind, so there was nothing on the chart to click, and the
 * marker's own context menu items were unreachable.
 *
 * This walks it as a first-time user would: insert from the command bar, click
 * the chip in the foot band, drag it, rename it in place.
 *
 * Written with iteration v2.28.0 and NOT EXECUTED in it: the owner deferred the
 * end-to-end suite to after the release (task brief 2026-10-29). The unit
 * cover for the same three rows — the gesture driven through the shipped
 * component — is `tests/unit/markerFoot.test.ts`, which passes.
 */

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/**
 * The chip in the foot band, which is where every marker's name now lives.
 *
 * `.last()`, not `.first()`: the sample ships a marker of its own — mk-gate,
 * "Gate review" — and `addMarker` appends, so `.first()` always resolved the
 * sample's and never the one the test had just inserted. Every assertion below
 * was about the wrong marker.
 */
function chip(page: Page) {
  return page.locator('rect[data-pp-kind="marker"][rx="4"]').last();
}

/**
 * Where the chip sits relative to the chart, in client pixels.
 *
 * `toBeVisible()` is no use here: a clipped SVG node still reports visible,
 * because a clip-path does not shrink a bounding box. That is exactly how the
 * shipped build passed its own assertions while drawing the band at y=1632 in
 * a plot that ends at 826, with nothing on screen at all.
 */
async function chipGeometry(page: Page) {
  return page.evaluate(() => {
    const el = Array.from(document.querySelectorAll('rect[data-pp-kind="marker"][rx="4"]')).pop();
    const area = document.querySelector('.chart-area');
    if (!el || !area) return null;
    const c = el.getBoundingClientRect();
    const a = area.getBoundingClientRect();
    return { chipTop: c.top, chipBottom: c.bottom, areaTop: a.top, areaBottom: a.bottom };
  });
}

test.describe('a deadline marker is editable (SRS-ELEM-051)', () => {
  test('the chip in the foot band carries the name, and selects on a click', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    await expect(page.locator('text=MARKERS')).toBeVisible();
    await expect(page.getByText('Deadline', { exact: true })).toBeVisible();

    // The assertion the old version could not make: on a plan far taller than
    // the window, the chip is still ON SCREEN, in the band at the foot of the
    // visible plot (SRS-ELEM-062). Bounded both ways — a band drawn at the TOP
    // of the plot, over row 0, is the defect SRS-ELEM-053 was written against,
    // and `chipTop > areaTop` alone would call that a pass.
    const g = await chipGeometry(page);
    expect(g, 'the chip has a box to measure').not.toBeNull();
    expect(g!.chipBottom).toBeLessThanOrEqual(g!.areaBottom + 1);
    expect(g!.chipTop).toBeGreaterThan(g!.areaBottom - 120);

    await chip(page).click();
    await expect(page.locator('.app-status')).toContainText('1 selected');
  });

  test('the chip stays put while the plan scrolls under it', async ({ page, viewport }) => {
    // The chart scrolls on a wheel, and mobile WebKit has no wheel to send —
    // `page.mouse.wheel` throws there outright. The pin itself is covered on
    // every project by the geometry assertion in the test above; this one adds
    // the scroll-invariance half, which needs a scroll to exist.
    test.skip((viewport?.width ?? 1280) < 1000, 'mobile WebKit has no mouse wheel');
    await ready(page);
    await page.getByTitle('Add deadline marker').click();
    const before = await chipGeometry(page);
    expect(before).not.toBeNull();

    // Scroll a long way down the plan. The band is pinned to the window, so
    // its screen position must not move; a band that scrolled with the rows
    // would leave the viewport entirely, which is what shipped.
    const box = (await page.locator('.chart-area').boundingBox())!;
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    for (let i = 0; i < 6; i++) await page.mouse.wheel(0, 200);
    await page.waitForTimeout(150);

    const after = await chipGeometry(page);
    expect(after).not.toBeNull();
    expect(Math.abs(after!.chipTop - before!.chipTop)).toBeLessThan(2);
    expect(after!.chipBottom).toBeLessThanOrEqual(after!.areaBottom + 1);
  });

  test('dragging the chip moves the date, and one undo puts it back', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    // `page.mouse` has no auto-scroll, unlike `locator.click()`. At phone
    // width the app shell is 702px wide inside a 390px viewport, so the chip
    // sits past the right edge and a raw mouse press lands on nothing at all
    // (`elementsFromPoint` there returns []).
    await chip(page).scrollIntoViewIfNeeded();
    const before = await chip(page).boundingBox();
    expect(before).not.toBeNull();
    await page.mouse.move(before!.x + before!.width / 2, before!.y + before!.height / 2);
    await page.mouse.down();
    await page.mouse.move(before!.x + before!.width / 2 + 120, before!.y + before!.height / 2, { steps: 8 });
    // The live readout is the requirement, not a nicety (SR-IXC-04).
    await expect(page.locator('rect[rx="9"]')).toBeVisible();
    await page.mouse.up();

    const after = await chip(page).boundingBox();
    expect(after!.x).toBeGreaterThan(before!.x + 40);

    await page.keyboard.press('Control+z');
    const undone = await chip(page).boundingBox();
    expect(Math.abs(undone!.x - before!.x)).toBeLessThan(2);
  });

  test('a double-click renames it in place (SRS-ELEM-053)', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    await chip(page).dblclick();
    const editor = page.locator('input.inline-editor');
    await expect(editor).toBeVisible();
    // Not "Gate review": the sample already carries a marker by that name, so
    // the old version renamed "Deadline" to a string that was already on the
    // chart, and `updateMarker` discards a rename to the same value anyway —
    // the text assertion passed on the sample's own chip either way.
    await editor.fill('Phase gate');
    await editor.press('Enter');

    await expect(page.getByText('Phase gate', { exact: true })).toBeVisible();
    await expect(editor).toHaveCount(0);
  });

  test('F2 renames it too, which is what its own menu promises', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();
    await chip(page).click();
    await expect(page.locator('.app-status')).toContainText('1 selected');

    // The marker's context menu has always printed F2 beside Rename, and the
    // keymap refused anything that was not a task (SR-IXC-16).
    await page.keyboard.press('F2');
    const editor = page.locator('input.inline-editor');
    await expect(editor).toBeVisible();
    await editor.fill('Readiness gate');
    await editor.press('Enter');
    await expect(page.getByText('Readiness gate', { exact: true })).toBeVisible();
  });

  test('right-click offers the menu that used to be unreachable', async ({ page }) => {
    await ready(page);
    await page.getByTitle('Add deadline marker').click();

    await chip(page).click({ button: 'right' });
    await expect(page.getByRole('menuitem', { name: /Rename/ })).toBeVisible();
    await expect(page.getByRole('menuitem', { name: /Delete marker/ })).toBeVisible();
  });
});
