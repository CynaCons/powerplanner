/**
 * Label placement under space pressure.
 *
 * The renderer used to fall back from `on-bar` to `right` whenever a label was
 * wider than its bar, with no check that the space to the right was free. On a
 * dense chart every bar is a sliver, so every label spilled right and landed on
 * top of the next bar's label — "doesn't fit" was being turned into "overlap
 * the neighbour" instead of "don't draw it" (PLAN.md v2.12.0 / L0a).
 *
 * This module makes that decision explicitly, from the space actually
 * available, and is pure so it can be tested without a DOM.
 */

/** Average glyph advance at 11px in the UI font, measured empirically. */
export const CHAR_PX = 6.1;

/** Padding inside a bar before on-bar text is considered to fit. */
const ON_BAR_PADDING = 16;
/** Gap kept between a right-placed label and the next bar. */
const RIGHT_GUTTER = 10;
/** Below this width a label is noise rather than information. */
const MIN_LEGIBLE_PX = 26;

export type Placement = 'on-bar' | 'left' | 'right' | 'above' | 'below' | 'hidden';

export interface FitInput {
  text: string;
  /** Requested placement from the document model. */
  requested: Placement;
  barWidth: number;
  /** Free horizontal space between this bar's right edge and the next item in
   *  the same lane. `Infinity` when nothing follows. */
  spaceRight: number;
}

export interface FitResult {
  placement: Placement;
  /** Text to draw — may be truncated with an ellipsis. */
  text: string;
  /** True when the text was shortened, so the caller can expose the full value. */
  truncated: boolean;
}

export function textWidth(text: string): number {
  return text.length * CHAR_PX;
}

/** Shorten to fit `maxPx`, with an ellipsis. Returns null if nothing legible fits. */
export function truncateToWidth(text: string, maxPx: number): string | null {
  if (maxPx < MIN_LEGIBLE_PX) return null;
  if (textWidth(text) <= maxPx) return text;
  const maxChars = Math.floor(maxPx / CHAR_PX) - 1;
  if (maxChars < 3) return null;
  return text.slice(0, maxChars).trimEnd() + '…';
}

/**
 * Decide where a task label goes and what it says.
 *
 * Explicit placements from the document are honoured — the user asked for them.
 * Only the default `on-bar` degrades, and it degrades to *hidden* rather than
 * to an overlap.
 */
export function fitLabel({ text, requested, barWidth, spaceRight }: FitInput): FitResult {
  if (requested === 'hidden' || text.length === 0) {
    return { placement: 'hidden', text: '', truncated: false };
  }

  // An explicit non-default placement is a user decision, not a guess. Respect
  // it; above/below/left have their own lanes and do not fight bar neighbours.
  if (requested !== 'on-bar') {
    return { placement: requested, text, truncated: false };
  }

  const full = textWidth(text);

  // 1. Fits inside the bar — the best case, and the only one that reads as a
  //    label *of* the bar rather than a note beside it.
  if (full <= barWidth - ON_BAR_PADDING) {
    return { placement: 'on-bar', text, truncated: false };
  }

  // 2. Fits in the free space to the right without touching the next item.
  const usableRight = spaceRight - RIGHT_GUTTER;
  if (full <= usableRight) {
    return { placement: 'right', text, truncated: false };
  }

  // 3. Doesn't fit anywhere in full. Truncate into whichever side offers more
  //    room, preferring on-bar so the text stays attached to its bar.
  const onBarRoom = barWidth - ON_BAR_PADDING;
  if (onBarRoom >= usableRight) {
    const clipped = truncateToWidth(text, onBarRoom);
    if (clipped) return { placement: 'on-bar', text: clipped, truncated: true };
  }
  const clippedRight = truncateToWidth(text, usableRight);
  if (clippedRight) return { placement: 'right', text: clippedRight, truncated: true };

  // 4. Nothing legible fits. Drawing it anyway is what produced the unreadable
  //    chart; the label remains reachable via hover and the inspector.
  return { placement: 'hidden', text: '', truncated: true };
}

export interface LaneItem {
  /** Left edge in px. */
  x: number;
  /** Right edge in px. */
  right: number;
  /** Lane key — items only compete with others in the same row + sub-row. */
  lane: string;
}

/**
 * For each item, the free horizontal space before the next item in its lane.
 *
 * Items are grouped by lane and sorted by x, so a task only yields space to the
 * bar that actually follows it on screen — not to one two lanes below.
 */
export function computeSpaceRight<T extends LaneItem>(items: T[]): Map<T, number> {
  const byLane = new Map<string, T[]>();
  for (const item of items) {
    const bucket = byLane.get(item.lane);
    if (bucket) bucket.push(item);
    else byLane.set(item.lane, [item]);
  }

  const result = new Map<T, number>();
  for (const bucket of byLane.values()) {
    const sorted = [...bucket].sort((a, b) => a.x - b.x);
    for (let i = 0; i < sorted.length; i++) {
      const next = sorted[i + 1];
      result.set(sorted[i], next ? Math.max(0, next.x - sorted[i].right) : Infinity);
    }
  }
  return result;
}
