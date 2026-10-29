import { create } from 'zustand';
import type { ISODate } from '../types/document';
import { addDays } from '../utils/dates';

/** The zoom range the chart is usable over. Also the clamp the anchor maths reads. */
const PX_PER_DAY_MIN = 0.05;
const PX_PER_DAY_MAX = 120;

function clampPxPerDay(px: number): number {
  return Math.max(PX_PER_DAY_MIN, Math.min(PX_PER_DAY_MAX, px));
}

interface ViewportState {
  startDate: ISODate;
  pxPerDay: number;
  /**
   * The plot's width in px, published by the chart (SRS-TAX-051).
   *
   * A zoom invoked from a button or a key has no pointer to hold still, so it
   * holds the middle of the plot — and the store is the only place that can
   * know where the middle is without every caller being handed the geometry.
   */
  chartWidth: number;
  setChartWidth: (px: number) => void;
  /** Vertical scroll of the chart body, in px from the top. Never negative. */
  scrollY: number;
  /** Scroll by `dy`, clamped to the content. `max` is contentH - viewportH. */
  scrollBy: (dy: number, max: number) => void;
  /**
   * A row the chart should bring into view on its next pass (SRS-EDIT-028).
   *
   * An insert used to land on `doc.rows[0]`, often scrolled off the top, so it
   * looked like nothing had happened. Now that it lands on the planner's row
   * it is usually already visible — but a section's new child may not be, and
   * an item you cannot see is the same bug wearing a different hat.
   */
  revealRowId: string | null;
  reveal: (rowId: string) => void;
  clearReveal: () => void;
  setStart: (iso: ISODate) => void;
  setPxPerDay: (px: number) => void;
  pan: (dx: number) => void;
  /**
   * Scale by `factor`, holding one date still. `anchorPx` is measured from the
   * left edge of the plot; omitting it holds the middle of the plot.
   */
  zoom: (factor: number, anchorPx?: number) => void;
  fit: (start: ISODate, end: ISODate, widthPx: number) => void;
}

export const useViewportStore = create<ViewportState>((set, get) => ({
  startDate: '2026-05-25',
  pxPerDay: 8,
  // A sane width to anchor against before the chart has measured itself; the
  // first ResizeObserver pass replaces it.
  chartWidth: 800,
  setChartWidth: (px) => {
    // Guarded because the chart assigns this on every layout pass, and an
    // unguarded `set` wakes every subscriber with an identical value.
    if (get().chartWidth !== px) set({ chartWidth: px });
  },
  scrollY: 0,
  // Clamped at both ends: with nothing to scroll, max is 0 and the view does
  // not move at all, rather than drifting into blank space.
  scrollBy: (dy, max) =>
    set((s) => ({ scrollY: Math.max(0, Math.min(Math.max(0, max), s.scrollY + dy)) })),
  revealRowId: null,
  reveal: (rowId) => set({ revealRowId: rowId }),
  clearReveal: () => set({ revealRowId: null }),
  setStart: (iso) => set({ startDate: iso }),
  setPxPerDay: (px) => set({ pxPerDay: clampPxPerDay(px) }),
  pan: (dx) => {
    const s = get();
    const days = dx / s.pxPerDay;
    set({ startDate: addDays(s.startDate, -Math.round(days)) });
  },
  zoom: (factor, anchorPx) => {
    const s = get();
    // No pointer, no anchor: hold the middle of what is on screen
    // (SRS-TAX-051). Callers that have a pointer pass its offset.
    const anchor = anchorPx ?? s.chartWidth / 2;
    // Clamp first, so the shift is computed from the scale actually applied.
    // At either end of the range the factor is partly or wholly refused, and a
    // shift derived from the asked-for scale would slide the view for a zoom
    // that did not happen.
    const newPx = clampPxPerDay(s.pxPerDay * factor);
    // One rounding, from the closed form, per SRS-TAX-052. Naming the anchor
    // date and then placing the new start rounded twice, and at 120 px/day a
    // lost day is 120px: the point under the cursor crept away a notch at a
    // time, which is what "zoom where my pointer is" was failing to deliver.
    const shiftDays = Math.round(anchor / s.pxPerDay - anchor / newPx);
    set({ pxPerDay: newPx, startDate: addDays(s.startDate, shiftDays) });
  },
  fit: (start, end, widthPx) => {
    const days = Math.max(1, Math.round((new Date(end).getTime() - new Date(start).getTime()) / 86_400_000));
    const padding = Math.ceil(days * 0.1);
    const px = widthPx / (days + padding * 2);
    set({ startDate: addDays(start, -padding), pxPerDay: clampPxPerDay(px) });
  },
}));
