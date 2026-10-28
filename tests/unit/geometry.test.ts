import { describe, it, expect } from 'vitest';
import { clientToChart, chartToClient, rowAtY, type ChartGeometry } from '../../src/layout/geometry';
import { layoutDocument } from '../../src/layout/engine';
import { sampleDocument } from '../../src/utils/sample';

/**
 * SRS-EDIT-033 — a REGRESSION test, not a gate.
 *
 * `clientToChart` did not exist before the fix, so this could never have been
 * watched going red. The gate is `tests/e2e/marquee-geometry.spec.ts`, which
 * sweeps the real chart and was red on 090ddae.
 */

const layout = layoutDocument({ doc: sampleDocument(), viewStart: '2026-01-21', pxPerDay: 0.71 });
const rect = { left: 24, top: 96 };

function geom(over: Partial<ChartGeometry> = {}): ChartGeometry {
  return { gutter: 200, axisH: 48, scrollY: 0, chartWidth: 1240, maxScroll: 952, layout, ...over };
}

describe('chart geometry', () => {
  it('round-trips at rest and when scrolled', () => {
    for (const g of [geom(), geom({ scrollY: 952 }), geom({ gutter: 120 }), geom({ axisH: 64, scrollY: 300 })]) {
      for (const [cx, cy] of [[300, 200], [24, 96], [1264, 820]]) {
        const p = clientToChart(g, rect, cx, cy);
        const back = chartToClient(g, rect, p.x, p.y);
        expect([back.x, back.y]).toEqual([cx, cy]);
      }
    }
  });

  it('carries the scroll, which is what the four hand-rolled copies did not', () => {
    const at0 = clientToChart(geom(), rect, 500, 400);
    const at600 = clientToChart(geom({ scrollY: 600 }), rect, 500, 400);
    expect(at600.y - at0.y).toBe(600);
    expect(at600.x).toBe(at0.x);
  });

  it('a wider gutter moves the origin, it does not move the content', () => {
    const narrow = clientToChart(geom({ gutter: 120 }), rect, 500, 400);
    const wide = clientToChart(geom({ gutter: 200 }), rect, 500, 400);
    expect(narrow.x - wide.x).toBe(80);
  });

  it('finds the row a content y lands in, and none outside the sheet', () => {
    const firstRow = layout.visibleRows[0].id;
    expect(rowAtY(layout.rowOffsets[0] + 1, layout)).toBe(firstRow);
    expect(rowAtY(-1, layout)).toBeNull();
    expect(rowAtY(layout.chartHeight + 1, layout)).toBeNull();
  });
});
