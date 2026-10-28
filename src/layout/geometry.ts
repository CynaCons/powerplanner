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
