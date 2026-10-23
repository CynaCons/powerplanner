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

/**
 * `auto-right` is the renderer's default, not a document value. The bar style
 * puts labels *outside* the bar (PLAN.md v2.16 — owner chose direction B), but
 * an outside label is only safe where the space beside the bar is actually
 * free. Asking for a bare `right` would bypass the collision check entirely
 * and reproduce the overlap this module exists to prevent, so the default
 * prefers right, falls back on-bar, and only then truncates or hides.
 */
export type RequestedPlacement = Placement | 'auto-right';

export interface FitInput {
  text: string;
  /** Requested placement from the document model, or the renderer default. */
  requested: RequestedPlacement;
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
  if (requested !== 'on-bar' && requested !== 'auto-right') {
    return { placement: requested, text, truncated: false };
  }

  const full = textWidth(text);

  // The default bar style wants the label beside the bar, but only where the
  // lane is clear. Preference order: right if free, then on-bar if it fits,
  // then the shared truncation path below.
  if (requested === 'auto-right') {
    if (full <= spaceRight - RIGHT_GUTTER) {
      return { placement: 'right', text, truncated: false };
    }
    if (full <= barWidth - ON_BAR_PADDING) {
      return { placement: 'on-bar', text, truncated: false };
    }
  }

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
  //    chart; the label remains reachable on hover.
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

/* ------------------------------------------------------------------ *
 * Placement allocation across a lane
 * ------------------------------------------------------------------ */

export interface LabelCandidate {
  id: string;
  /** Centre of the anchor (a milestone diamond) in px. */
  x: number;
  text: string;
  /** Labels only compete with others laid into the same lane. */
  lane: string;
  /** An explicit document choice. Honoured as given, never re-fitted. */
  requested?: Placement;
}

export interface LabelAllocation {
  placement: Placement;
  text: string;
  truncated: boolean;
}

/**
 * Order the allocator tries when the document has not chosen a placement.
 *
 * Right first because it reads as "label of this marker" most strongly; then
 * the vertical lanes, which a diamond leaves completely free; left last,
 * because a label to the left of its own marker reads as belonging to whatever
 * precedes it.
 */
export const AUTO_ORDER: Placement[] = ['right', 'above', 'below', 'left'];

/** Horizontal span a label would occupy at a given placement. */
function extent(place: Placement, x: number, w: number, anchorHalf: number, gap: number): [number, number] {
  switch (place) {
    case 'right':
      return [x + anchorHalf + gap, x + anchorHalf + gap + w];
    case 'left':
      return [x - anchorHalf - gap - w, x - anchorHalf - gap];
    default: // above / below are centred on the anchor
      return [x - w / 2, x + w / 2];
  }
}

function overlaps(a: [number, number], spans: [number, number][]): boolean {
  return spans.some(([s, e]) => a[0] < e && a[1] > s);
}

/**
 * Assign each label a placement that does not collide with another label in
 * the same lane.
 *
 * Milestone labels used to be fitted one at a time against the free space to
 * their right only, so a crowded lane either truncated them or dropped them —
 * even though the space directly above and below a diamond was empty (owner
 * report 2026-10-23). Allocating across the lane lets a blocked label step up
 * or down instead of disappearing.
 *
 * Pure, so the behaviour is testable without a DOM.
 */
export function allocateLabels(
  items: LabelCandidate[],
  anchorHalf = 11,
  gap = 6,
  order: Placement[] = AUTO_ORDER,
): Map<string, LabelAllocation> {
  const out = new Map<string, LabelAllocation>();
  // lane -> placement -> occupied horizontal spans
  const taken = new Map<string, Map<Placement, [number, number][]>>();

  const claim = (lane: string, place: Placement, span: [number, number]) => {
    let byPlace = taken.get(lane);
    if (!byPlace) taken.set(lane, (byPlace = new Map()));
    const spans = byPlace.get(place) ?? [];
    spans.push(span);
    byPlace.set(place, spans);
  };
  const free = (lane: string, place: Placement, span: [number, number]) =>
    !overlaps(span, taken.get(lane)?.get(place) ?? []);

  for (const item of [...items].sort((a, b) => a.x - b.x)) {
    if (item.requested === 'hidden' || item.text.length === 0) {
      out.set(item.id, { placement: 'hidden', text: '', truncated: false });
      continue;
    }

    const w = textWidth(item.text);

    // An explicit placement is a user decision. It is recorded as occupancy so
    // automatic labels route around it, but it is never moved itself.
    if (item.requested) {
      claim(item.lane, item.requested, extent(item.requested, item.x, w, anchorHalf, gap));
      out.set(item.id, { placement: item.requested, text: item.text, truncated: false });
      continue;
    }

    const place = order.find((p) => free(item.lane, p, extent(p, item.x, w, anchorHalf, gap)));
    if (place) {
      claim(item.lane, place, extent(place, item.x, w, anchorHalf, gap));
      out.set(item.id, { placement: place, text: item.text, truncated: false });
    } else {
      // Every lane is taken. Drop it rather than overlap — the rule this
      // module exists for.
      out.set(item.id, { placement: 'hidden', text: '', truncated: true });
    }
  }

  return out;
}
