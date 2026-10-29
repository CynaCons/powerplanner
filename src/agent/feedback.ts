/**
 * What an agent can see of the roadmap it is editing (SRS-ABR-033/034/035).
 *
 * Owner directive 2026-10-29: "make sure all actions are doable by agents using
 * mcp, and that we get live feedback on the roadmap ui". The bridge already had
 * a render, but it was not feedback in any useful sense:
 *
 *  - `get_render` serialised the live `<svg>` node and stopped there. Every
 *    colour in that markup is a `var(--color-…)` reference to a custom property
 *    declared on `:root` in `src/styles/global.css`, which is OUTSIDE the
 *    serialised subtree. So the markup rendered as black-on-transparent
 *    wherever it rendered at all, and SRS-ABR-031 was satisfied only in the
 *    letter: the bytes were a chart, the picture was not.
 *  - The MCP server returns every payload as one text block, so an agent
 *    received SVG source to read rather than an image to look at.
 *  - Nothing reported what was on SCREEN. The document says a marker exists;
 *    it does not say the marker's name is rendered a thousand pixels below the
 *    visible chart area, which is exactly the defect the v2.28.0 walkthrough
 *    found by hand. An agent had no way to notice it.
 *
 * This module is deliberately separate from `AgentBridge` and free of bridge
 * types: it is three pure-ish readers over the live DOM and the stores, so they
 * can be unit-tested without a socket.
 */

import { useDocumentStore } from '../stores/documentStore';
import { useViewportStore } from '../stores/viewportStore';
import { useSelectionStore } from '../stores/selectionStore';
import { addDays } from '../utils/dates';

/** Longest side of the raster, in CSS pixels. */
const MAX_RASTER_EDGE = 1200;
/**
 * A base64 PNG above this is not worth sending: it costs an agent more context
 * than the whole document does, for a picture it only has to glance at.
 */
const MAX_PNG_CHARS = 1_400_000;
/** How long to wait for the browser to decode the serialised chart. */
const RASTER_TIMEOUT_MS = 2_000;

export interface OffScreenCensus {
  /** How many of this kind are drawn wholly inside the visible chart area. */
  visible: number;
  /**
   * How many have some of their parts on screen and some off.
   *
   * This bucket exists because of a defect it would otherwise have missed. A
   * deadline marker is drawn as a full-height rule plus a name chip in a band
   * at the foot of the plot. On a 39-row plan the rule is plainly visible and
   * the chip is a thousand pixels below the fold — so judging the marker by any
   * one of its elements calls it visible, and the thing the owner could not
   * reach (the name, and the double-click on it that renames it) does not
   * register at all.
   */
  clipped: number;
  /** How many are wholly outside it, by the edge they are past. */
  above: number;
  below: number;
  left: number;
  right: number;
}

export interface ViewSummary {
  startDate: string;
  endDate: string;
  pxPerDay: number;
  scale: string;
  gridDelimiter: string;
  weekendDisplay: string;
  selection: Array<{ kind: string; id: string }>;
  /**
   * The visible chart area in CSS pixels, and how much plan lies past it.
   *
   * `contentBelow` / `contentAbove` are measured from the drawn elements, not
   * from the SVG node: the chart scrolls by translating its contents inside a
   * window-sized `<svg>`, so the node's own height is always the window's and
   * says nothing about how much plan there is.
   */
  viewportPx: {
    width: number;
    /** The plot, right of the row panel — what the date window maps onto. */
    plotWidth: number;
    height: number;
    scrollY: number;
    contentAbove: number;
    contentBelow: number;
  };
  onScreen: Record<string, OffScreenCensus>;
  /**
   * False in a host with no layout engine, where every rect is zero and the
   * census cannot mean anything. Stated rather than hidden, so a caller is
   * never handed a confident "everything is off screen" from a host that
   * simply does not know.
   */
  hasLayout: boolean;
  /**
   * Plain-language warnings about things a person would notice and an agent
   * would not. Empty when everything the document declares is on screen.
   */
  warnings: string[];
}

export interface RenderResult {
  svg: string;
  /** Base64 PNG (no data: prefix), or absent when rasterising was refused. */
  png?: string;
  pngError?: string;
  width: number;
  height: number;
}

/** Just enough of a DOMRect to decide where something sits. */
export interface Box {
  top: number;
  bottom: number;
  left: number;
  right: number;
}

/**
 * Bucket every drawn element into per-kind counts, grouped by the entity it
 * belongs to.
 *
 * Pure, and exported, because the interesting rule here cannot be exercised
 * through the DOM in a test host: jsdom reports every rect as zero, so the one
 * case worth proving — an entity with some parts on screen and some off — is
 * unreachable from `viewSummary` under vitest.
 */
export function censusOf(
  items: Array<{ kind: string; id: string; rect: Box }>,
  area: Box,
): Record<string, OffScreenCensus> {
  const parts = new Map<string, { kind: string; inside: number; edges: Record<string, number> }>();
  for (const { kind, id, rect } of items) {
    const key = `${kind}:${id}`;
    const entry = parts.get(key) ?? { kind, inside: 0, edges: {} };
    if (rect.bottom <= area.top) entry.edges.above = (entry.edges.above ?? 0) + 1;
    else if (rect.top >= area.bottom) entry.edges.below = (entry.edges.below ?? 0) + 1;
    else if (rect.right <= area.left) entry.edges.left = (entry.edges.left ?? 0) + 1;
    else if (rect.left >= area.right) entry.edges.right = (entry.edges.right ?? 0) + 1;
    else entry.inside += 1;
    parts.set(key, entry);
  }

  const census: Record<string, OffScreenCensus> = {};
  const bump = (kind: string, field: keyof OffScreenCensus) => {
    const row = (census[kind] ??= { visible: 0, clipped: 0, above: 0, below: 0, left: 0, right: 0 });
    row[field] += 1;
  };
  for (const { kind, inside, edges } of parts.values()) {
    const outside = Object.values(edges).reduce((a, b) => a + b, 0);
    if (outside === 0) bump(kind, 'visible');
    else if (inside > 0) bump(kind, 'clipped');
    else {
      // Wholly outside: attribute it to the edge most of it is past.
      const worst = Object.entries(edges).sort((a, b) => b[1] - a[1])[0][0];
      bump(kind, worst as keyof OffScreenCensus);
    }
  }
  return census;
}

function chartSvg(): SVGSVGElement {
  const svg =
    document.querySelector('.chart-area svg') ?? document.querySelector('svg');
  if (!svg) throw new Error('no chart SVG present in the document');
  return svg as SVGSVGElement;
}

function chartArea(): Element | null {
  return document.querySelector('.chart-area');
}

/**
 * Serialise the chart so it stands on its own.
 *
 * The custom properties are discovered from the markup rather than listed here.
 * A hardcoded list is a second copy of the theme that goes stale the first time
 * a renderer reaches for a token nobody remembered to add — and it goes stale
 * silently, because the missing colour renders as black and black is a
 * plausible colour for a chart.
 */
export function serializeChartSvg(): { svg: string; width: number; height: number } {
  const live = chartSvg();
  const rect = live.getBoundingClientRect();
  const width = Math.max(1, Math.round(rect.width || live.clientWidth || 800));
  const height = Math.max(1, Math.round(rect.height || live.clientHeight || 600));

  const clone = live.cloneNode(true) as SVGSVGElement;
  clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
  clone.setAttribute('xmlns:xlink', 'http://www.w3.org/1999/xlink');
  clone.setAttribute('width', String(width));
  clone.setAttribute('height', String(height));
  if (!clone.getAttribute('viewBox')) {
    clone.setAttribute('viewBox', `0 0 ${width} ${height}`);
  }

  const markup = new XMLSerializer().serializeToString(clone);
  const root = getComputedStyle(document.documentElement);
  const body = getComputedStyle(document.body);

  const used = new Set<string>();
  for (const match of markup.matchAll(/var\(\s*(--[A-Za-z0-9_-]+)/g)) used.add(match[1]);

  const declarations: string[] = [];
  for (const name of [...used].sort()) {
    const value = root.getPropertyValue(name).trim() || body.getPropertyValue(name).trim();
    // An unresolvable token is left out on purpose: `--x: ;` is a parse error in
    // some renderers, and leaving it absent keeps the renderer's own fallback,
    // which every `var(--x, fallback)` in this codebase supplies.
    if (value) declarations.push(`${name}: ${value}`);
  }

  // The page's background, not the SVG's: the chart is drawn on the app shell
  // and has no ground of its own, so a raster without this is text floating on
  // transparency.
  const pageBg = body.backgroundColor || root.getPropertyValue('--color-bg').trim() || '#ffffff';
  const fontFamily = body.fontFamily || 'system-ui, sans-serif';

  const style =
    `<style>:root{${declarations.join(';')}}` +
    `svg{background:${pageBg};font-family:${fontFamily.replace(/"/g, "'")}}</style>`;

  // Injected after the opening tag so it precedes every element that reads it.
  const svg = markup.replace(/^(<svg[^>]*>)/, `$1${style}`);
  return { svg, width, height };
}

/**
 * Rasterise the serialised chart to a PNG an agent can look at.
 *
 * Returns the reason instead of the bytes when the browser refuses: a tainted
 * canvas throws on `toDataURL`, and an agent told "rasterising failed because
 * the canvas was tainted" can act on that, whereas one handed a silent `svg`
 * field would believe it had seen the chart.
 */
export async function rasterizeChart(
  svg: string,
  width: number,
  height: number,
): Promise<{ png?: string; pngError?: string }> {
  const scale = Math.min(1, MAX_RASTER_EDGE / Math.max(width, height));
  const w = Math.max(1, Math.round(width * scale));
  const h = Math.max(1, Math.round(height * scale));

  try {
    const url = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(svg)}`;
    const image = new Image();
    image.decoding = 'sync';
    // Raced against a deadline, because neither callback is guaranteed to fire:
    // a host without a real image decoder never calls either one, and the bridge
    // would then hold the call open until the server's own timeout and report
    // TIMEOUT — an error about the transport for what is really a missing
    // capability. Found by tests/unit/agentFeedback.test.ts hanging for 5s.
    const decoded = await new Promise<boolean>((resolve) => {
      const timer = setTimeout(() => resolve(false), RASTER_TIMEOUT_MS);
      image.onload = () => {
        clearTimeout(timer);
        resolve(true);
      };
      image.onerror = () => {
        clearTimeout(timer);
        resolve(false);
      };
      image.src = url;
    });
    if (!decoded) {
      return { pngError: 'this host could not decode the serialised chart into an image' };
    }

    const canvas = document.createElement('canvas');
    canvas.width = w;
    canvas.height = h;
    const ctx = canvas.getContext('2d');
    if (!ctx) return { pngError: 'no 2d canvas context in this host' };
    ctx.drawImage(image, 0, 0, w, h);

    const dataUrl = canvas.toDataURL('image/png');
    const base64 = dataUrl.slice(dataUrl.indexOf(',') + 1);
    if (base64.length > MAX_PNG_CHARS) {
      return {
        pngError:
          `the raster was ${Math.round(base64.length / 1024)}KB encoded, over the ${Math.round(
            MAX_PNG_CHARS / 1024,
          )}KB cap; narrow the viewport or zoom out and call get_render again`,
      };
    }
    return { png: base64 };
  } catch (err) {
    return { pngError: err instanceof Error ? err.message : String(err) };
  }
}

export async function renderChart(): Promise<RenderResult> {
  const { svg, width, height } = serializeChartSvg();
  const raster = await rasterizeChart(svg, width, height);
  return { svg, width, height, ...raster };
}

/**
 * What the roadmap looks like right now, as data.
 *
 * The census is taken from the live DOM rather than computed from the layout,
 * because the question an agent needs answered is not "where would this be
 * drawn" but "can the person see it". Those differ whenever something is
 * clipped, scrolled past, or parked in a band below the last row.
 */
export function viewSummary(): ViewSummary {
  const doc = useDocumentStore.getState().doc;
  const vp = useViewportStore.getState();
  const area = chartArea();
  const svg = (() => {
    try {
      return chartSvg();
    } catch {
      return null;
    }
  })();

  const areaRect = area?.getBoundingClientRect();
  const width = Math.round(areaRect?.width ?? 0);
  const height = Math.round(areaRect?.height ?? 0);

  const hasLayout = !!areaRect && (areaRect.width > 0 || areaRect.height > 0);

  // Grouped by entity, not counted per element: several elements share one id
  // (a bar is a body, two grips and a progress handle; a marker is a
  // full-height rule plus a name chip), so counting elements would report five
  // tasks where there is one.
  const items: Array<{ kind: string; id: string; rect: Box }> = [];
  if (svg && areaRect) {
    for (const el of svg.querySelectorAll<SVGGraphicsElement>('[data-pp-id][data-pp-kind]')) {
      const id = el.dataset.ppId;
      const kind = el.dataset.ppKind;
      if (!id || !kind) continue;
      items.push({ kind, id, rect: el.getBoundingClientRect() });
    }
  }
  const onScreen = areaRect ? censusOf(items, areaRect) : {};

  // How much plan lies past each edge, measured from what is drawn.
  let contentAbove = 0;
  let contentBelow = 0;
  if (areaRect && items.length > 0) {
    const topMost = Math.min(...items.map((i) => i.rect.top));
    const bottomMost = Math.max(...items.map((i) => i.rect.bottom));
    contentAbove = Math.max(0, Math.round(areaRect.top - topMost));
    contentBelow = Math.max(0, Math.round(bottomMost - areaRect.bottom));
  }

  const warnings: string[] = [];
  for (const [kind, c] of Object.entries(hasLayout ? onScreen : {})) {
    const off = c.above + c.below + c.left + c.right;
    const total = off + c.clipped + c.visible;
    if (off === 0 && c.clipped === 0) continue;
    const where = [
      c.above ? `${c.above} above` : '',
      c.below ? `${c.below} below` : '',
      c.left ? `${c.left} before the view` : '',
      c.right ? `${c.right} after it` : '',
    ]
      .filter(Boolean)
      .join(', ');
    if (off > 0) {
      warnings.push(
        c.visible === 0 && c.clipped === 0
          ? `every ${kind} is off screen (${where}); the person cannot see any of them`
          : `${off} of ${total} ${kind} elements are off screen (${where})`,
      );
    }
    if (c.clipped > 0) {
      // Said separately and in these words because this is the case a person
      // reports as "I cannot edit it" while every count looks healthy.
      warnings.push(
        `${c.clipped} of ${total} ${kind} elements are only partly on screen — some of what they are ` +
          `drawn from, such as a name or a label, is outside the view and cannot be clicked`,
      );
    }
  }
  if (hasLayout && (contentBelow > 1 || contentAbove > 1)) {
    const parts = [
      contentAbove > 1 ? `${contentAbove}px above it` : '',
      contentBelow > 1 ? `${contentBelow}px below it` : '',
    ].filter(Boolean);
    warnings.push(`the plan extends past the ${height}px window: ${parts.join(' and ')}`);
  }

  // The date window spans the PLOT, not the chart area: the row panel occupies
  // the left of that box and carries no dates, so dividing the whole width by
  // px/day overstated the window by the panel's width — about a third of it on
  // a narrow pane. The panel publishes its measured width to this token for
  // exactly this kind of reader (ChartArea, SRS-LAY-050).
  const gutterPx = (() => {
    const raw = getComputedStyle(document.documentElement).getPropertyValue('--row-gutter-w').trim();
    const n = Number.parseFloat(raw);
    return Number.isFinite(n) ? n : 0;
  })();
  const plotWidth = Math.max(0, width - gutterPx);
  const days = plotWidth > 0 && vp.pxPerDay > 0 ? Math.round(plotWidth / vp.pxPerDay) : 0;

  return {
    startDate: vp.startDate,
    endDate: days > 0 ? addDays(vp.startDate, days) : vp.startDate,
    pxPerDay: Math.round(vp.pxPerDay * 1000) / 1000,
    scale: doc.calendar.scale,
    gridDelimiter: doc.calendar.gridDelimiter ?? 'week',
    weekendDisplay: doc.calendar.weekendDisplay ?? 'shaded',
    selection: useSelectionStore.getState().items.map((i) => ({ kind: i.kind, id: i.id })),
    viewportPx: {
      width,
      plotWidth: Math.round(plotWidth),
      height,
      // From the viewport store, not `area.scrollTop`: the chart body is
      // `overflow: hidden` and scrolls by translating its contents, so the
      // element's own scrollTop is structurally always 0 — a number that looks
      // like an answer and can never move.
      scrollY: Math.round(vp.scrollY),
      contentAbove,
      contentBelow,
    },
    onScreen,
    hasLayout,
    warnings,
  };
}
