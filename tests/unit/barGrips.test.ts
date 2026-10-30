import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { createElement, act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { TaskBars } from '../../src/renderer/TaskBars';
import { hitRank, resolveHit, carriedY, rowBandAt, pctFromX } from '../../src/layout/geometry';
import { layoutDocument, BAR_HEIGHT, ROW_HEIGHT } from '../../src/layout/engine';
import { useDocumentStore } from '../../src/stores/documentStore';
import { useSelectionStore } from '../../src/stores/selectionStore';
import { useEditStore } from '../../src/stores/editStore';
import { useViewStore } from '../../src/stores/viewStore';
import { useViewportStore } from '../../src/stores/viewportStore';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-EDIT-070, SRS-EDIT-071 — the end of a selected bar is its resize grip.
 *
 * The two dependency anchors were centred on the bar's end caps, `cx={x}` and
 * `cx={x + width}` at `r=5`, with each 5px resize grip sitting entirely
 * underneath one of them. So the end of a SELECTED bar belonged to the anchor
 * and resizing only worked while the bar was not selected: "we should first
 * select, but then when we select, we have the anchor to do the dependency…
 * we are missing that option to resize."
 *
 * Moving the anchors is only half of it. `Dependencies` renders after
 * `TaskBars` and lays a transparent 10px stroke along each connector as its
 * hit target, so a connector crossing a bar end took the press anyway. The
 * ranking in `geometry` is what stops a painting decision deciding what a
 * gesture means.
 *
 * Observed red on 79ccbdb: anchor cx exactly equal to the bar's x and to
 * x+width, and the grips drawn inside the bar at the same two pixels.
 */

declare global {
  var IS_REACT_ACT_ENVIRONMENT: boolean;
}

/** Must match TaskBars: a grip straddles the end cap, an anchor leads off it. */
const GRIP_W = 7;
const ANCHOR_LEAD = 16;
const ANCHOR_R = 5;

function doc(): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Grips',
    calendar: { scale: 'day', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [
      { id: 'r1', label: 'Row 1', groupId: null },
      { id: 'r2', label: 'Row 2', groupId: null },
    ],
    tasks: [
      { id: 't1', rowId: 'r1', label: 'Architecture', start: '2026-06-08', end: '2026-06-12', percentComplete: 40 },
    ],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

let root: Root;
let host: HTMLDivElement;

function mount(selected: boolean) {
  const d = doc();
  act(() => {
    useDocumentStore.getState().setDocument(d);
    useSelectionStore.setState({ items: selected ? [{ kind: 'task', id: 't1' }] : [] });
  });
  const layout = layoutDocument({ doc: d, viewStart: '2026-06-01', pxPerDay: 10 });
  act(() => {
    root.render(createElement('svg', null, createElement(TaskBars, { layout, dragPreview: null })));
  });
  return layout;
}

const num = (el: Element | null, attr: string) => Number(el?.getAttribute(attr) ?? NaN);

beforeEach(() => {
  globalThis.IS_REACT_ACT_ENVIRONMENT = true;
  useEditStore.setState({ editingTaskId: null, snapEnabled: true });
  useViewStore.setState({ showCriticalPath: false });
  useViewportStore.setState({ startDate: '2026-06-01', pxPerDay: 10 });
  host = document.createElement('div');
  document.body.appendChild(host);
  root = createRoot(host);
});

afterEach(() => {
  act(() => root.unmount());
  host.remove();
});

describe('a selected bar keeps its ends (SRS-EDIT-070)', () => {
  it('puts a resize grip on each end cap', () => {
    const layout = mount(true);
    const bar = layout.tasks[0];

    const start = host.querySelector('[data-pp-kind="task"][data-pp-sub="start"]');
    const end = host.querySelector('[data-pp-kind="task"][data-pp-sub="end"]');
    expect(start, 'no start grip').not.toBeNull();
    expect(end, 'no end grip').not.toBeNull();

    // The grip straddles the cap, so the cap itself is inside the target.
    expect(num(start, 'x')).toBeLessThanOrEqual(bar.x);
    expect(num(start, 'x') + num(start, 'width')).toBeGreaterThanOrEqual(bar.x);
    expect(num(end, 'x')).toBeLessThanOrEqual(bar.x + bar.width);
    expect(num(end, 'x') + num(end, 'width')).toBeGreaterThanOrEqual(bar.x + bar.width);

    // And it is the full row band tall, because 11px of chrome is not 11px of
    // target — the drawn grip is thinner than the thing you have to hit.
    expect(num(start, 'height')).toBe(bar.height);
  });

  it('floats the dependency anchors clear of both grips', () => {
    const layout = mount(true);
    const bar = layout.tasks[0];

    const anchors = Array.from(host.querySelectorAll('[data-pp-kind="connector"]'));
    expect(anchors).toHaveLength(2);

    const cxs = anchors.map((a) => num(a, 'cx')).sort((a, b) => a - b);
    expect(cxs[0]).toBe(bar.x - ANCHOR_LEAD);
    expect(cxs[1]).toBe(bar.x + bar.width + ANCHOR_LEAD);

    // The one thing that has to hold: no pixel is claimed by both. An anchor
    // reaches ANCHOR_R towards the bar, a grip reaches half its width away
    // from it, and what is left between them is the clearance.
    for (const cx of cxs) {
      const cap = Math.abs(cx - bar.x) < Math.abs(cx - (bar.x + bar.width)) ? bar.x : bar.x + bar.width;
      expect(Math.abs(cx - cap)).toBeGreaterThan(ANCHOR_R + GRIP_W / 2);
    }
  });

  it('leaves the grips reachable with the bar unselected too', () => {
    mount(false);
    // The grips are the gesture, not the selection chrome: hiding them until
    // the bar is selected is the round trip SR-IXC-13 asks to be removed.
    expect(host.querySelectorAll('[data-pp-kind="task"][data-pp-sub="start"]')).toHaveLength(1);
    expect(host.querySelectorAll('[data-pp-kind="task"][data-pp-sub="end"]')).toHaveLength(1);
    // The anchors belong to selection and stay behind it.
    expect(host.querySelectorAll('[data-pp-kind="connector"]')).toHaveLength(0);
  });

  it('gives up the grips rather than the bar body on a very short bar', () => {
    const d = doc();
    d.tasks[0].end = d.tasks[0].start; // one day, 10px at this zoom
    act(() => useDocumentStore.getState().setDocument(d));
    const layout = layoutDocument({ doc: d, viewStart: '2026-06-01', pxPerDay: 10 });
    act(() => {
      root.render(createElement('svg', null, createElement(TaskBars, { layout, dragPreview: null })));
    });

    const grips = host.querySelectorAll('[data-pp-sub="start"],[data-pp-sub="end"]');
    const body = host.querySelector('.task-bar-body');
    expect(body, 'the body carries the rename double-click and must survive').not.toBeNull();
    for (const g of grips) expect(num(g, 'width')).toBeLessThanOrEqual(layout.tasks[0].width - 8);
  });
});

describe('what a press belongs to (SRS-EDIT-071)', () => {
  it('ranks a DRAWN resize grip above a dependency hit band', () => {
    expect(hitRank('task', 'start', '1')).toBeGreaterThan(hitRank('dependency', undefined));
    expect(hitRank('task', 'end', '1')).toBeGreaterThan(hitRank('dependency', undefined));
    // And above the bar body, which the grip sits on top of.
    expect(hitRank('task', 'start', '1')).toBeGreaterThan(hitRank('task', undefined));
    // The progress handle is more specific than the body and less than a grip,
    // so the two extremes of the fill belong to resizing.
    expect(hitRank('task', 'progress', '1')).toBeLessThan(hitRank('task', 'start', '1'));
    expect(hitRank('task', 'progress', '1')).toBeGreaterThan(hitRank('task', undefined));
  });

  /**
   * SRS-EDIT-079 — a grip you cannot see does not take the press.
   *
   * The grips are hit-testable at every zoom; they are only DRAWN on a
   * selected or hovered bar. So an invisible 10px rect straddled each end cap
   * and took presses aimed at whatever crossed it, while showing an
   * `ew-resize` cursor it would not honour. Observed on 2026-10-30: a press
   * on a visible connector resolved to `task-sysarch sub=end`, rank 60
   * against the connector's 10, with nothing but the connector in
   * `elementsFromPoint` before the press.
   */
  it('ranks an UNDRAWN grip below the connector you can see', () => {
    expect(hitRank('task', 'start', undefined)).toBeLessThan(hitRank('dependency', undefined));
    expect(hitRank('task', 'end', undefined)).toBeLessThan(hitRank('dependency', undefined));
    // Still above nothing, so approaching a bar end from outside — where the
    // grip is the only thing under the pointer — still begins a resize.
    expect(hitRank('task', 'start', undefined)).toBeGreaterThan(hitRank('row', undefined));
    expect(hitRank('task', 'start', undefined)).toBeGreaterThan(0);
  });

  it('picks the drawn grip out of a stack a dependency is painted on top of', () => {
    const svg = document.createElement('div');
    svg.innerHTML =
      '<span data-pp-kind="dependency" data-pp-id="d1"></span>' +
      '<span data-pp-kind="task" data-pp-id="t1" data-pp-sub="start" data-pp-live="1"></span>' +
      '<span data-pp-kind="task" data-pp-id="t1"></span>';
    const [dep, grip] = Array.from(svg.children);

    // The stack is front to back, exactly as `elementsFromPoint` returns it,
    // and the dependency is in front because it is painted last.
    const picked = resolveHit([dep, grip], dep);
    expect(picked?.dataset.ppKind).toBe('task');
    expect(picked?.dataset.ppSub).toBe('start');
  });

  it('leaves the connector selectable where the grip is not drawn', () => {
    const svg = document.createElement('div');
    svg.innerHTML =
      '<span data-pp-kind="dependency" data-pp-id="d1"></span>' +
      '<span data-pp-kind="task" data-pp-id="t1" data-pp-sub="start"></span>';
    const [dep, grip] = Array.from(svg.children);
    expect(resolveHit([dep, grip], dep)?.dataset.ppKind).toBe('dependency');
    // Alone, it is still the grip: nothing visible is competing for the press.
    expect(resolveHit([grip], grip)?.dataset.ppSub).toBe('start');
  });

  it('falls back to the event target where the stack is unavailable', () => {
    const svg = document.createElement('div');
    svg.innerHTML = '<span data-pp-kind="milestone" data-pp-id="m1"><i></i></span>';
    const inner = svg.querySelector('i')!;
    // jsdom has no `elementsFromPoint`, and so does any browser the gesture
    // reaches before layout settles; the ranking must be an improvement on the
    // event target, never a replacement for having one.
    expect(resolveHit([], inner)?.dataset.ppId).toBe('m1');
    expect(resolveHit([], null)).toBeNull();
  });
});

describe('the geometry a carried element previews at (SRS-EDIT-072)', () => {
  const layout = layoutDocument({
    doc: {
      ...doc(),
      tasks: [
        { id: 't1', rowId: 'r1', label: 'A', start: '2026-06-01', end: '2026-06-03' },
        { id: 't2', rowId: 'r1', label: 'B', start: '2026-06-05', end: '2026-06-07' },
        { id: 't3', rowId: 'r1', label: 'C', start: '2026-06-02', end: '2026-06-06' },
      ],
    },
    viewStart: '2026-06-01',
    pxPerDay: 10,
  });

  it('carries the offset inside the row, so a stacked bar stays stacked', () => {
    const r1 = rowBandAt(layout, 'r1')!;
    expect(r1.height).toBeGreaterThan(ROW_HEIGHT); // r1 stacked to two tracks

    const secondTrack = r1.top + ROW_HEIGHT + (ROW_HEIGHT - BAR_HEIGHT) / 2;
    const landed = carriedY(layout, 'r1', 'r1', secondTrack, 0, BAR_HEIGHT);
    expect(landed).toBe(secondTrack);
  });

  it('clamps into a shorter row rather than previewing below it', () => {
    const r1 = rowBandAt(layout, 'r1')!;
    const r2 = rowBandAt(layout, 'r2')!;
    const secondTrack = r1.top + ROW_HEIGHT + (ROW_HEIGHT - BAR_HEIGHT) / 2;

    const landed = carriedY(layout, 'r1', 'r2', secondTrack, 0, BAR_HEIGHT);
    expect(landed).toBeGreaterThanOrEqual(r2.top);
    expect(landed + BAR_HEIGHT).toBeLessThanOrEqual(r2.top + r2.height);
  });

  it('leaves the y alone when either row is out of view', () => {
    expect(carriedY(layout, 'r1', 'nope', 40)).toBe(40);
    expect(carriedY(layout, 'gone', 'r2', 40)).toBe(40);
  });
});

describe('the percentage a progress drag asks for (SRS-EDIT-074)', () => {
  it('reads the pointer position along the bar, in whole percent', () => {
    expect(pctFromX(100, 100, 200)).toBe(0);
    expect(pctFromX(200, 100, 200)).toBe(50);
    expect(pctFromX(300, 100, 200)).toBe(100);
    expect(pctFromX(237, 100, 200)).toBe(69); // 68.5, rounded — the pill says 69
  });

  it('clamps outside the bar instead of running past 0 or 100', () => {
    expect(pctFromX(-50, 100, 200)).toBe(0);
    expect(pctFromX(9999, 100, 200)).toBe(100);
    // A two-pixel bar has no meaningful span; 0 is the safe reading.
    expect(pctFromX(50, 100, 0)).toBe(0);
  });
});
