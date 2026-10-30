import type { LayoutResult } from './engine';

/**
 * The chart's one coordinate transform (SRS-EDIT-033).
 *
 * The chart paints its body inside `translate(gutter, axisH - scrollY)`. Every
 * gesture needs that transform's inverse, and before this module existed each
 * one re-derived it by subtraction at the point of use — four copies, each
 * wrong in a different way: the press had the live gutter but no `scrollY`;
 * the move was pinned by an empty dependency array to the first render's
 * 120px gutter, 80px short of the 200px the chart actually draws; the hit test
 * compared against a `layout` captured before the initial auto-fit.
 *
 * So the transform is written down once, here, and read through a ref at the
 * moment of the event rather than captured into a long-lived window listener.
 * A ref is what makes `useCallback(..., [])` on those listeners correct rather
 * than stale — re-creating them mid-drag would leak the old listener, because
 * the attach/detach pair removes by function reference.
 */
export interface ChartGeometry {
  /** Width of the row list on the left. */
  gutter: number;
  /** Height of the calendar header: the sum of the bands the axis built. */
  axisH: number;
  /** How far the body is scrolled under the header. */
  scrollY: number;
  /** Width of the plot area, right of the gutter. */
  chartWidth: number;
  /** The furthest the body can scroll. */
  maxScroll: number;
  /** The layout the chart is currently painting. */
  layout: LayoutResult;
}

export interface Point {
  x: number;
  y: number;
}

/**
 * Where a pointer is, in the content space the layout is expressed in.
 *
 * `rect` is the chart SVG's own client rectangle.
 */
export function clientToChart(geom: ChartGeometry, rect: { left: number; top: number }, clientX: number, clientY: number): Point {
  return {
    x: clientX - rect.left - geom.gutter,
    y: clientY - rect.top - geom.axisH + geom.scrollY,
  };
}

/** The exact inverse, for probing the document at a content coordinate. */
export function chartToClient(geom: ChartGeometry, rect: { left: number; top: number }, x: number, y: number): Point {
  return {
    x: x + geom.gutter + rect.left,
    y: y + geom.axisH - geom.scrollY + rect.top,
  };
}

/** The row under a content y, or null above the first row and below the last. */
export function rowAtY(y: number, layout: LayoutResult): string | null {
  for (let i = 0; i < layout.rowOffsets.length; i++) {
    const top = layout.rowOffsets[i];
    const bottom = top + layout.rowHeights[i];
    if (y >= top && y < bottom) return layout.visibleRows[i].id;
  }
  return null;
}

/** The band a row occupies, or null when that row is collapsed out of view. */
export function rowBandAt(layout: LayoutResult, rowId: string): { top: number; height: number } | null {
  const i = layout.visibleRows.findIndex((r) => r.id === rowId);
  if (i < 0) return null;
  return { top: layout.rowOffsets[i], height: layout.rowHeights[i] };
}

/**
 * Where an element previews when a drag carries it to another row
 * (SRS-EDIT-072).
 *
 * The offset inside the row is carried across rather than recomputed, so a bar
 * on the second stacking track of a crowded row stays on the second track
 * while it is in flight — the engine only re-stacks on commit, and a preview
 * that snapped every carried bar to the top track would redraw it twice.
 *
 * It is clamped because rows are not all the same height: a row with three
 * stacking tracks is 108px and one with a single task is 36px, so an offset
 * carried blind out of the tall row would preview the bar below the band it is
 * about to join. `above` and `below` are how much room the glyph needs either
 * side of the y it is drawn at — a bar is anchored at its band top, a
 * milestone at its centre.
 */
export function carriedY(
  layout: LayoutResult,
  fromRowId: string,
  toRowId: string,
  y: number,
  above = 0,
  below = 0,
): number {
  const from = rowBandAt(layout, fromRowId);
  const to = rowBandAt(layout, toRowId);
  if (!from || !to) return y;
  const lo = above;
  const hi = Math.max(above, to.height - below);
  return to.top + Math.min(Math.max(y - from.top, lo), hi);
}

/**
 * How specific a chart element is as the subject of a press (SRS-EDIT-071).
 *
 * Painting order decided this before, and painting order is a visual decision:
 * `Dependencies` renders after `TaskBars` and lays a transparent 10px-wide
 * stroke along every connector as its hit target, so a connector crossing a
 * bar end took the press that was aimed at the resize grip under it. A grip is
 * 7px wide and a dependency's hit band is 10px of nothing, so the widest,
 * vaguest target was winning the most precise gesture.
 *
 * Higher wins. The order is by how small and how deliberate the target is, not
 * by where it sits in the tree.
 */
export function hitRank(
  kind: string | undefined,
  sub: string | undefined,
  /**
   * Whether this grip is actually drawn (SRS-EDIT-079). A bar's resize grips and progress
   * handle are hit-testable at every zoom, but only *visible* on a selected
   * or hovered bar — so an invisible 10px rect straddling each bar end was
   * taking presses aimed at whatever crossed it, and showing an `ew-resize`
   * cursor over a connector it would not resize. Measured 2026-10-30: a
   * press on a visible connector at 1.44 px/day resolved to
   * `task-sysarch sub=end`, rank 60 against the connector's 10, with nothing
   * but the connector in `elementsFromPoint` before the press.
   *
   * Undrawn, a grip still wins when it is the only thing under the pointer —
   * approaching a bar's end from outside still begins a resize — but it no
   * longer outranks ink the planner can see.
   */
  live?: string | undefined,
): number {
  if (!kind) return 0;
  const grip = kind === 'task' && (sub === 'start' || sub === 'end' || sub === 'progress');
  if (grip && live !== '1') return 8;
  if (kind === 'task' && (sub === 'start' || sub === 'end')) return 60;
  if (kind === 'task' && sub === 'progress') return 50;
  if (kind === 'connector') return 40;
  if (kind === 'bracket' && (sub === 'start' || sub === 'end')) return 35;
  if (kind === 'milestone' || kind === 'marker' || kind === 'label') return 30;
  if (kind === 'task' || kind === 'bracket') return 20;
  // A connector's hit target is the whole length of the line, so it only ever
  // wins where nothing more specific is under the pointer.
  if (kind === 'dependency') return 10;
  if (kind === 'row') return 5;
  return 1;
}

/**
 * The element a press belongs to, out of everything stacked under the pointer.
 *
 * `fallback` is what `e.target.closest(...)` found. It is used when the stack
 * is empty, which is the case in jsdom and in any environment where
 * `elementsFromPoint` is not implemented: the ranking is an improvement on the
 * event target, never a replacement for having one.
 */
export function resolveHit(stack: Element[], fallback: Element | null): HTMLElement | null {
  let best: HTMLElement | null = null;
  let bestRank = 0;
  for (const el of stack) {
    const owner = el.closest('[data-pp-kind]') as HTMLElement | null;
    if (!owner) continue;
    const rank = hitRank(owner.dataset.ppKind, owner.dataset.ppSub, owner.dataset.ppLive);
    // Strictly greater, so a tie keeps the topmost — the stack is ordered front
    // to back, and between two equals the one on top is the one being pointed at.
    if (rank > bestRank) {
      best = owner;
      bestRank = rank;
    }
  }
  return best ?? ((fallback?.closest('[data-pp-kind]') as HTMLElement | null) ?? null);
}

/**
 * The percentage a progress drag is asking for (SRS-EDIT-074).
 *
 * Whole percent, because that is what the readout beside the handle says and
 * the two must not disagree — a pill reading 42% over a bar holding 42.37 is
 * the same class of lie as a preview that does not match the commit.
 */
export function pctFromX(pointerX: number, barX: number, barWidth: number): number {
  if (barWidth <= 0) return 0;
  const raw = ((pointerX - barX) / barWidth) * 100;
  return Math.min(100, Math.max(0, Math.round(raw)));
}
