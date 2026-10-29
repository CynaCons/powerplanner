import { describe, it, expect, beforeEach } from 'vitest';
import { useViewportStore } from '../../src/stores/viewportStore';
import { diffDays, addDays } from '../../src/utils/dates';

/**
 * Where a zoom leaves the view (SRS-TAX-051, SRS-TAX-052).
 *
 * The owner, on 2026-10-29: "with the mouse scroll zoom, I expect to zoom
 * where my mouse pointer is. That's the expectation." Two separate things were
 * stopping that. The arithmetic rounded twice, so each notch lost up to a day
 * at each end, and at 120 px/day a day is 120px of drift. And only the wheel
 * ever passed a real anchor — every other path passed the literal 400, which
 * is the middle of the plot on an 800px chart and nowhere else.
 */
describe('viewportStore.zoom', () => {
  beforeEach(() => {
    useViewportStore.setState({ startDate: '2026-10-01', pxPerDay: 8, chartWidth: 1000, scrollY: 0 });
  });

  /** The date currently drawn at `anchorPx` from the left edge of the plot. */
  const dateAt = (anchorPx: number) => {
    const s = useViewportStore.getState();
    return addDays(s.startDate, Math.round(anchorPx / s.pxPerDay));
  };

  it('holds the date under the anchor', () => {
    const before = dateAt(300);
    useViewportStore.getState().zoom(1.15, 300);
    // One rounding is still a rounding: a day either way is the floor.
    expect(Math.abs(diffDays(before, dateAt(300)))).toBeLessThanOrEqual(1);
  });

  it('does not walk the anchor away over a run of notches', () => {
    const before = dateAt(300);
    for (let i = 0; i < 10; i++) useViewportStore.getState().zoom(1.15, 300);
    // The old two-rounding form drifted cumulatively; this stays within the
    // half-day each single rounding can cost.
    expect(Math.abs(diffDays(before, dateAt(300)))).toBeLessThanOrEqual(2);
  });

  it('comes back to the same date on a zoom round trip', () => {
    const before = dateAt(420);
    useViewportStore.getState().zoom(2, 420);
    useViewportStore.getState().zoom(0.5, 420);
    expect(useViewportStore.getState().pxPerDay).toBeCloseTo(8, 6);
    expect(Math.abs(diffDays(before, dateAt(420)))).toBeLessThanOrEqual(1);
  });

  it('holds the left edge still when the anchor is the left edge', () => {
    useViewportStore.getState().zoom(1.15, 0);
    expect(useViewportStore.getState().startDate).toEqual('2026-10-01');
  });

  it('anchors on the middle of the plot when no pointer is given', () => {
    const middle = dateAt(500);
    useViewportStore.getState().zoom(1.25);
    expect(Math.abs(diffDays(middle, dateAt(500)))).toBeLessThanOrEqual(1);
  });

  it('follows the chart width the chart reports, not a constant', () => {
    useViewportStore.getState().setChartWidth(400);
    const middle = dateAt(200);
    useViewportStore.getState().zoom(1.25);
    expect(Math.abs(diffDays(middle, dateAt(200)))).toBeLessThanOrEqual(1);
  });

  it('does not move the view for a zoom the clamp refused', () => {
    // At the ceiling there is no scale change left, so there is no shift
    // either — deriving it from the asked-for factor would slide the view for
    // a zoom that did not happen.
    useViewportStore.setState({ pxPerDay: 120 });
    const start = useViewportStore.getState().startDate;
    useViewportStore.getState().zoom(1.15, 300);
    expect(useViewportStore.getState().pxPerDay).toEqual(120);
    expect(useViewportStore.getState().startDate).toEqual(start);

    useViewportStore.setState({ pxPerDay: 0.05 });
    const floorStart = useViewportStore.getState().startDate;
    useViewportStore.getState().zoom(0.8, 300);
    expect(useViewportStore.getState().pxPerDay).toEqual(0.05);
    expect(useViewportStore.getState().startDate).toEqual(floorStart);
  });

  it('keeps pxPerDay inside the usable range', () => {
    for (let i = 0; i < 60; i++) useViewportStore.getState().zoom(1.5, 100);
    expect(useViewportStore.getState().pxPerDay).toEqual(120);
    for (let i = 0; i < 200; i++) useViewportStore.getState().zoom(0.5, 100);
    expect(useViewportStore.getState().pxPerDay).toEqual(0.05);
  });
});

describe('viewportStore.setChartWidth', () => {
  it('ignores a repeat of the width it already has', () => {
    useViewportStore.setState({ chartWidth: 900 });
    let notified = 0;
    const stop = useViewportStore.subscribe(() => { notified++; });
    useViewportStore.getState().setChartWidth(900);
    expect(notified).toEqual(0);
    useViewportStore.getState().setChartWidth(901);
    expect(notified).toEqual(1);
    stop();
  });
});
