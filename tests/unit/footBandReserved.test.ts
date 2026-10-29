import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { footBandTop } from '../../src/renderer/Markers';

/**
 * SRS-LAY-052, SRS-ELEM-062 — the band under the last row is reserved, not
 * borrowed, and it stays on screen.
 *
 * Two labels were being drawn past the foot of the last row, where
 * `chart-plot-clip` removes them: the below-label of SRS-EDIT-056 and the
 * deadline chips at the foot of the chart. The first attempt took the band out
 * of the last 30px of the plot, which puts it over that row's own bars. The
 * second reserved it correctly — and then pinned it to the foot of the PLOT,
 * which on any plan taller than the window is outside the clip, so the band
 * was cut away and nothing was drawn at all.
 *
 * Observed red on 79ccbdb: `ChartArea` had no `FOOT_BAND_H`. Observed red on
 * d101e0b: `footBandTop` took the chart height alone and returned
 * `chartHeight - 30`, which for the sample's 1584px of rows is 1554 — 728px
 * below a plot that ends at 826.
 */

const chart = readFileSync('src/app/ChartArea.tsx', 'utf8');
const BAND_H = 30;

describe('the foot band is extra height, not the last row (SRS-LAY-052)', () => {
  it('starts exactly where the rows end, while they fit the window', () => {
    // 420px of rows in a 900px body: the band runs 420..450, below the last
    // row rather than over it.
    expect(footBandTop(420, 900)).toBe(420);
  });

  it('reserves the band in the sheet, so the clips include it', () => {
    expect(chart).toContain('const FOOT_BAND_H = 30;');
    expect(chart).toContain('AXIS_TOTAL_H + layout.chartHeight + FOOT_BAND_H');
    // Both clips derive their height from sheetBottom, so reserving it there
    // is what keeps the band from being drawn and then cut away.
    expect(chart).toContain('sheetBottom - AXIS_TOTAL_H');
  });

  it('lets the scroll reach the foot of the plan', () => {
    // The reservation is still what a below-row label on the LAST row needs:
    // that label scrolls with its own row (SRS-EDIT-056) and would otherwise
    // be clipped away, which is the defect this height was added for.
    expect(chart).toContain('const plotHeight = layout.chartHeight + FOOT_BAND_H;');
    expect(chart).toContain('Math.max(0, plotHeight - bodyHeight)');
  });

  it('hands Markers the rows and the window, not the plot height', () => {
    // The pin needs all three: where the rows end, how much the window can
    // show, and how far the plot has scrolled under it.
    expect(chart).toContain('chartHeight={layout.chartHeight}');
    expect(chart).toContain('bodyHeight={bodyHeight}');
    expect(chart).toContain('scrollY={scrollY}');
    expect(chart).not.toContain('chartHeight={plotHeight}');
  });

  it('keeps the band the same height in both files', () => {
    const markers = readFileSync('src/renderer/Markers.tsx', 'utf8');
    expect(markers).toContain('const BAND_H = 30;');
  });
});

describe('the band stays on screen (SRS-ELEM-062)', () => {
  it('never leaves the window, however tall the plan', () => {
    // The property that matters, asserted across the range rather than at one
    // convenient fixture: wherever the band lands, its foot is inside the body
    // the window can actually show.
    for (const rows of [0, 36, 420, 726, 1584, 20000]) {
      for (const body of [200, 726, 900]) {
        const top = footBandTop(rows, body);
        expect(top).toBeGreaterThanOrEqual(0);
        expect(top + BAND_H).toBeLessThanOrEqual(Math.max(body, rows + BAND_H));
        expect(top).toBeLessThanOrEqual(body);
      }
    }
  });

  it('is pinned, not scrolled', () => {
    // A band that scrolled with the rows would return the same local y at
    // every offset; a pinned one moves with the offset to cancel the parent
    // transform out. This is the assertion that fails if someone reverts the
    // pin, and it cannot be satisfied by ignoring the scroll.
    expect(footBandTop(1584, 726, 0)).toBe(696);
    expect(footBandTop(1584, 726, 300)).toBe(996);
    expect(footBandTop(1584, 726, 888)).toBe(1584);
  });

  it('never returns a number SVG cannot use', () => {
    // On first paint the chart has not been measured, and a NaN here is not one
    // bad number but five bad attributes per marker per frame: the band rect's
    // y and height, the divider's y1/y2, the eyebrow's y, the chip's y and the
    // rule's y1/y2. `Math.max(0, NaN)` is NaN, so the clamps alone do not catch
    // it — which is why the inputs are screened. Reported by a session running
    // the dev server against a half-applied edit of this change, where
    // `bodyHeight` arrived undefined and produced 77 console errors.
    for (const bad of [NaN, undefined as unknown as number, Infinity, -Infinity]) {
      expect(Number.isFinite(footBandTop(bad, 726))).toBe(true);
      expect(Number.isFinite(footBandTop(1584, bad))).toBe(true);
      expect(Number.isFinite(footBandTop(1584, 726, bad))).toBe(true);
    }
    // And a screened input must not be read as a huge one: a band pinned past
    // the foot of the window is the defect this whole row exists to prevent.
    expect(footBandTop(NaN, 726)).toBe(0);
    expect(footBandTop(1584, NaN)).toBe(0);
  });

  it('does not pin a band the plan has room for', () => {
    // The short-plan case must not drift: with the rows well inside the body
    // the band belongs under the last row and must not jump to the foot of the
    // window, leaving a gap between the plan and its own labels.
    expect(footBandTop(108, 726)).toBe(108);
    expect(footBandTop(108, 726, 0)).not.toBe(726 - BAND_H);
  });
});
