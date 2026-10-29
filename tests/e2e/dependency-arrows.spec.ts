import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';

/**
 * SRS-ELEM-042 / SRS-ELEM-045 — connectors point forwards, and a selected one
 * has actions.
 *
 * On the shipped v2.21.0 build four of the six links in the sample chart drew
 * their arrowhead pointing back at the predecessor, because the elbow split at
 * the midpoint of the two endpoints and a bar is painted through its end day:
 * a task finishing on the day the next starts has its right cap one day right
 * of the successor's left cap, so the last leg ran backwards and the
 * `orient="auto"` marker rotated with it. Live DOM at the time:
 *   d="M 362.64 882 L 361.67 882 L 361.67 954 L 360.69 954"
 *
 * Selecting a connector also left the command bar empty: `SelectionGroup`
 * returned null for kind "dependency", so delete and the link type existed
 * only on the right-click menu.
 *
 * This spec is the live-surface half of `tests/unit/dependencyRouting.test.ts`,
 * which covers the geometry. Written with v2.27.0; not executed in that
 * iteration — the owner deferred the e2e suite to after the release.
 */

async function ready(page: Page) {
  await page.goto('/');
  await page.locator('.chart-area > svg').first().waitFor({ state: 'visible' });
  const dismiss = page.locator('[aria-label="Dismiss"]').first();
  if (await dismiss.isVisible().catch(() => false)) await dismiss.click();
}

/** The sign of a connector path's last segment: +1 rightwards, -1 leftwards. */
function lastLegDir(d: string): number {
  const pts = d
    .trim()
    .split(/(?=[ML])/)
    .map((s) => s.trim().replace(/^[ML]\s*/, ''))
    .filter(Boolean)
    .map((pair) => pair.split(/\s+/).map(Number));
  const a = pts[pts.length - 2];
  const b = pts[pts.length - 1];
  return Math.sign(b[0] - a[0]);
}

test.describe('dependency connectors', () => {
  test('every connector on the sample chart points at its successor', async ({ page }) => {
    await ready(page);

    const hits = page.locator('[data-pp-kind="dependency"]');
    const count = await hits.count();
    expect(count).toBeGreaterThan(0);

    for (let i = 0; i < count; i++) {
      const d = await hits.nth(i).getAttribute('d');
      expect(d, `connector ${i} has a path`).toBeTruthy();
      // Default links are finish-to-start, so the head enters rightwards. A
      // zero-length or backwards final leg is the shipped defect.
      expect(lastLegDir(d!), `connector ${i} last leg`).toBe(1);
    }
  });

  test('a connector keeps a stub at each end before it turns', async ({ page }) => {
    await ready(page);

    const d = await page.locator('[data-pp-kind="dependency"]').first().getAttribute('d');
    const pts = d!
      .trim()
      .split(/(?=[ML])/)
      .map((s) => s.trim().replace(/^[ML]\s*/, ''))
      .filter(Boolean)
      .map((pair) => pair.split(/\s+/).map(Number));

    const first = Math.hypot(pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]);
    const last = Math.hypot(
      pts[pts.length - 1][0] - pts[pts.length - 2][0],
      pts[pts.length - 1][1] - pts[pts.length - 2][1],
    );
    expect(first).toBeGreaterThanOrEqual(10);
    expect(last).toBeGreaterThanOrEqual(10);
  });

  test('selecting a connector puts its actions in the command bar', async ({ page }) => {
    await ready(page);

    await page.locator('[data-pp-kind="dependency"]').first().click({ force: true });

    const bar = page.locator('.cmdbar');
    await expect(bar.getByLabel('Link type')).toBeVisible();
    await expect(bar.getByRole('radio', { name: 'FS' })).toHaveAttribute('aria-checked', 'true');
    await expect(bar.getByLabel('Delete dependency')).toBeVisible();
  });

  test('the link type set in the bar reaches the chart', async ({ page }) => {
    await ready(page);

    const hit = page.locator('[data-pp-kind="dependency"]').first();
    await hit.click({ force: true });
    const before = await hit.getAttribute('d');

    await page.locator('.cmdbar').getByRole('radio', { name: 'FF' }).click();

    const after = await hit.getAttribute('d');
    expect(after).not.toBe(before);
    // Finish-to-finish enters the successor's right cap, so the head comes in
    // leftwards.
    expect(lastLegDir(after!)).toBe(-1);
  });

  test('delete in the bar removes the connector', async ({ page }) => {
    await ready(page);

    const hits = page.locator('[data-pp-kind="dependency"]');
    const before = await hits.count();
    await hits.first().click({ force: true });
    await page.locator('.cmdbar').getByLabel('Delete dependency').click();

    await expect(hits).toHaveCount(before - 1);
  });
});
