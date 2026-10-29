import { describe, it, expect } from 'vitest';
import { readFileSync } from 'node:fs';
import { footBandTop } from '../../src/renderer/Markers';

/**
 * SRS-LAY-052 — the band under the last row is reserved, not borrowed.
 *
 * Two labels were being drawn past the foot of the last row, where
 * `chart-plot-clip` removes them: the below-label of SRS-EDIT-056 and the
 * deadline chips at the foot of the chart. The first attempt took the band out
 * of the last 30px of the plot, which puts it over that row's own bars.
 *
 * Observed red on 79ccbdb: `ChartArea` had no `FOOT_BAND_H`, `sheetBottom` was
 * `AXIS_TOTAL_H + layout.chartHeight`, and `<Markers>` was handed
 * `layout.chartHeight`, so `footBandTop` returned `chartHeight - 30` — inside
 * the last row.
 */

const chart = readFileSync('src/app/ChartArea.tsx', 'utf8');

describe('the foot band is extra height, not the last row (SRS-LAY-052)', () => {
  it('starts exactly where the rows end', () => {
    // ChartArea hands Markers the plot height, which is the rows plus the
    // band, so the band's top lands on the foot of the last row.
    const rows = 420;
    expect(footBandTop(rows + 30)).toBe(rows);
  });

  it('would sit over the last row if the band were borrowed', () => {
    // The shape of the defect, kept as the contrast: handing Markers the row
    // height alone puts the band inside the rows.
    expect(footBandTop(420)).toBeLessThan(420);
  });

  it('reserves the band in the sheet, so the clips include it', () => {
    expect(chart).toContain('const FOOT_BAND_H = 30;');
    expect(chart).toContain('AXIS_TOTAL_H + layout.chartHeight + FOOT_BAND_H');
    // Both clips derive their height from sheetBottom, so reserving it there
    // is what keeps the band from being drawn and then cut away.
    expect(chart).toContain('sheetBottom - AXIS_TOTAL_H');
  });

  it('lets the scroll reach the band', () => {
    // A plan taller than the window could otherwise never show its own foot.
    expect(chart).toContain('const plotHeight = layout.chartHeight + FOOT_BAND_H;');
    expect(chart).toContain('Math.max(0, plotHeight - bodyHeight)');
  });

  it('hands Markers the plot height, not the row height', () => {
    expect(chart).toContain('chartHeight={plotHeight}');
    expect(chart).not.toContain('chartHeight={layout.chartHeight}');
  });

  it('keeps the band the same height in both files', () => {
    const markers = readFileSync('src/renderer/Markers.tsx', 'utf8');
    expect(markers).toContain('const BAND_H = 30;');
  });
});
