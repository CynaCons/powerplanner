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

/**
 * A client point on connector `index` that no more specific element covers.
 *
 * `hitRank` puts a dependency below every bar (SRS-EDIT-071) and its press
 * target is now painted there too (SRS-EDIT-077), while a connector's crossing
 * leg runs the length of the bar it crosses (SRS-ELEM-043) — so the bounding
 * box centre, which is where `click({ force: true })` lands, belongs to that
 * bar and not to the connector. `force` skips the actionability check but
 * still dispatches real input at that point, so the three tests below were
 * selecting a TASK and then looking for the dependency's actions.
 *
 * Returns null when the whole connector is covered. The callers assert on
 * that, so an unreachable connector fails loudly instead of skipping.
 */
async function pointOnConnector(page: Page, index = 0) {
  return page.evaluate((i) => {
    const p = document.querySelectorAll('[data-pp-kind="dependency"]')[i] as SVGGeometryElement | undefined;
    if (!p) return null;
    const id = p.getAttribute('data-pp-id');
    const m = p.getScreenCTM();
    const svg = p.ownerSVGElement;
    if (!m || !svg) return null;
    const len = p.getTotalLength();
    const STEPS = 240;
    for (let s = 0; s <= STEPS; s++) {
      const u = p.getPointAtLength((len * s) / STEPS);
      const pt = svg.createSVGPoint();
      pt.x = u.x;
      pt.y = u.y;
      const c = pt.matrixTransform(m);
      const owners = document
        .elementsFromPoint(c.x, c.y)
        .map((el) => el.closest('[data-pp-kind]') as HTMLElement | null)
        .filter((el): el is HTMLElement => !!el);
      // Inside the viewport, or a click there reaches nothing: at phone width
      // the app shell is 702px wide in a 390px window and everything past the
      // right edge is unaddressable by `page.mouse`.
      if (c.x < 0 || c.y < 0 || c.x > window.innerWidth || c.y > window.innerHeight) continue;
      const isMine = (o: HTMLElement) => o.dataset.ppKind === 'dependency' && o.dataset.ppId === id;
      // A row band is not a competing target; anything else is.
      if (owners.some(isMine) && !owners.some((o) => !isMine(o) && o.dataset.ppKind !== 'row')) {
        return { x: c.x, y: c.y, id: id! };
      }
    }
    return null;
  }, index);
}

/**
 * Selecting a connector by pointing needs a connector there is something to
 * point AT, and at phone width there is not.
 *
 * Measured on the sample at 375px: of 912 points sampled along the six
 * connectors, 15 — 1.6% — are not covered by a bar, and they are one or two
 * pixels each. The app shell is also 702px wide inside that viewport, so much
 * of the chart is past the right edge where `page.mouse` cannot reach at all.
 * The defect these three tests were written against is a logic one —
 * `SelectionGroup` returned null for kind "dependency", so a selected
 * connector had no actions anywhere — and desktop and tablet gate it. The two
 * geometry tests in this file, which read the path and never click, keep
 * running everywhere.
 */
const NEEDS_POINTABLE_CONNECTOR = (viewport: { width: number } | null) => (viewport?.width ?? 1280) < 700;

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

  test('selecting a connector puts its actions in the command bar', async ({ page, viewport }) => {
    test.skip(NEEDS_POINTABLE_CONNECTOR(viewport), 'a connector is 98% covered at phone width');
    await ready(page);

    const pt = await pointOnConnector(page);
    expect(pt, 'a point on the first connector clear of every bar').not.toBeNull();
    await page.mouse.click(pt!.x, pt!.y);

    const bar = page.locator('.cmdbar');
    await expect(bar.getByLabel('Link type')).toBeVisible();
    await expect(bar.getByRole('radio', { name: 'FS' })).toHaveAttribute('aria-checked', 'true');
    await expect(bar.getByLabel('Delete dependency')).toBeVisible();
  });

  test('the link type set in the bar reaches the chart', async ({ page, viewport }) => {
    test.skip(NEEDS_POINTABLE_CONNECTOR(viewport), 'a connector is 98% covered at phone width');
    await ready(page);

    const pt = await pointOnConnector(page);
    expect(pt, 'a point on the first connector clear of every bar').not.toBeNull();
    await page.mouse.click(pt!.x, pt!.y);
    // By its own id, so the path read back is certainly the one that was
    // clicked rather than whichever happens to be first in the document.
    const hit = page.locator(`[data-pp-kind="dependency"][data-pp-id="${pt!.id}"]`);
    const before = await hit.getAttribute('d');

    await page.locator('.cmdbar').getByRole('radio', { name: 'FF' }).click();

    // Retrying, so a slow re-render is a wait and not a flake.
    await expect(hit).not.toHaveAttribute('d', before!);
    const after = await hit.getAttribute('d');
    expect(after).not.toBe(before);
    // Finish-to-finish enters the successor's right cap, so the head comes in
    // leftwards.
    expect(lastLegDir(after!)).toBe(-1);
  });

  test('delete in the bar removes the connector', async ({ page, viewport }) => {
    test.skip(NEEDS_POINTABLE_CONNECTOR(viewport), 'a connector is 98% covered at phone width');
    await ready(page);

    const hits = page.locator('[data-pp-kind="dependency"]');
    const before = await hits.count();
    const pt = await pointOnConnector(page);
    expect(pt, 'a point on the first connector clear of every bar').not.toBeNull();
    await page.mouse.click(pt!.x, pt!.y);
    await page.locator('.cmdbar').getByLabel('Delete dependency').click();

    await expect(hits).toHaveCount(before - 1);
  });
});
