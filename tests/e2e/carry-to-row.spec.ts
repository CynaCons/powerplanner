import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-EDIT-072, SRS-EDIT-073, SRS-EDIT-076 — a drag carries an element to
 * another row, and shows it there while it does.
 *
 * Both drags read `clientX` and nothing whatever from `clientY`, so a row was
 * a property only the details dialog could change; and the previewed milestone
 * date was written into `dragPreview` and never read, so the diamond stayed
 * nailed to its old date for the whole gesture. The owner tried both and
 * reported both.
 *
 * NOT EXECUTED at the time of writing: the owner deferred the e2e suite to
 * after the release. Written now so the gate exists when it is run.
 */

const DESKTOP_ONLY = ({ viewport }: { viewport: { width: number } | null }) =>
  (viewport?.width ?? 1280) < 1000;

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/** Which row the DOM says a bar belongs to. */
const rowOf = (page: Page, id: string) =>
  page.locator(`.task-bar-body[data-pp-id="${id}"]`).first().getAttribute('data-pp-row');

test.describe('carrying an element to another row', () => {
  test.skip(DESKTOP_ONLY, 'pointer-driven; desktop only');

  test('a bar dragged down changes row', async ({ page }) => {
    await ready(page);
    const bar = page.locator('.task-bar-body').first();
    const id = (await bar.getAttribute('data-pp-id'))!;
    const before = await rowOf(page, id);
    const box = (await bar.boundingBox())!;

    // Two row heights down, horizontally still.
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + 72, { steps: 8 });
    await page.mouse.up();

    expect(await rowOf(page, id)).not.toBe(before);
  });

  test('the bar previews on the row it will land on, before the release', async ({ page }) => {
    await ready(page);
    const bar = page.locator('.task-bar-body').first();
    const box = (await bar.boundingBox())!;

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2 + 72, { steps: 8 });

    // SR-IXC-03: what is on screen now is what letting go commits.
    const mid = (await bar.boundingBox())!;
    expect(mid.y).toBeGreaterThan(box.y + 40);
    await page.mouse.up();
  });

  test('a milestone follows the cursor and keeps its row change', async ({ page }) => {
    await ready(page);
    const ms = page.locator('[data-pp-kind="milestone"]').first();
    const box = (await ms.boundingBox())!;

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.move(box.x + box.width / 2 + 60, box.y + box.height / 2 + 36, { steps: 10 });

    // The diamond itself, not just the date pill, is under the pointer.
    const mid = (await ms.boundingBox())!;
    expect(mid.x).toBeGreaterThan(box.x + 20);
    expect(mid.y).toBeGreaterThan(box.y + 20);

    await page.mouse.up();
    const after = (await ms.boundingBox())!;
    expect(after.y).toBeGreaterThan(box.y + 20);
  });

  test('a press that never moves changes nothing', async ({ page }) => {
    await ready(page);
    const bar = page.locator('.task-bar-body').first();
    const id = (await bar.getAttribute('data-pp-id'))!;
    const before = await rowOf(page, id);
    const box = (await bar.boundingBox())!;

    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    await page.mouse.up();

    expect(await rowOf(page, id)).toBe(before);
    await expect(page.locator('.app-status')).toContainText('1 selected');
  });
});
