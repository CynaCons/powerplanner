import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { createElement, useRef, act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { TaskBars } from '../../src/renderer/TaskBars';
import { Milestones } from '../../src/renderer/Milestones';
import { Dependencies, DependencyHits } from '../../src/renderer/Dependencies';
import { useChartInteractions } from '../../src/app/useChartInteractions';
import { layoutDocument } from '../../src/layout/engine';
import type { ChartGeometry } from '../../src/layout/geometry';
import { useDocumentStore } from '../../src/stores/documentStore';
import { useSelectionStore } from '../../src/stores/selectionStore';
import { useEditStore } from '../../src/stores/editStore';
import { useToolStore } from '../../src/stores/toolStore';
import { useViewStore } from '../../src/stores/viewStore';
import { useViewportStore } from '../../src/stores/viewportStore';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-EDIT-071 to SRS-EDIT-076 — the three verbs of this iteration, exercised
 * as gestures.
 *
 * Mousedown, mousemove, mouseup on the shipped components through the shipped
 * hook, not through helpers. The last time this project tested its primary
 * verb through helpers it shipped nineteen versions with the verb broken, and
 * every defect below is a verb the owner reached for and did not get:
 *
 *   - a resize grip under a dependency's invisible 10px hit stroke, which took
 *     the press because it is painted after the bars (SRS-EDIT-071);
 *   - a drag that read `clientX` and nothing at all from `clientY`, so no
 *     element could ever change row (SRS-EDIT-072);
 *   - a milestone whose previewed date was written into `dragPreview` and
 *     never read, so the diamond stayed put for the whole drag (SRS-EDIT-073);
 *   - progress with no gesture at all, only two ten-percent steppers that
 *     SR-IXC-01 forbids in terms (SRS-EDIT-074).
 *
 * Observed red on 79ccbdb: a press on a covered grip selected the dependency,
 * `rowId` unchanged after a drag two rows down, the milestone's rendered x
 * unchanged mid-drag, and no `[data-pp-sub="progress"]` node in the markup.
 */

declare global {
  var IS_REACT_ACT_ENVIRONMENT: boolean;
}

const VIEW_START = '2026-06-01';
const PX_PER_DAY = 10;

function doc(): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Gestures',
    calendar: { scale: 'day', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [
      { id: 'r1', label: 'Row 1', groupId: null },
      { id: 'r2', label: 'Row 2', groupId: null },
      { id: 'r3', label: 'Row 3', groupId: null },
    ],
    tasks: [
      { id: 't1', rowId: 'r1', label: 'Architecture', start: '2026-06-08', end: '2026-06-17', percentComplete: 40 },
      { id: 't2', rowId: 'r3', label: 'Integration', start: '2026-06-20', end: '2026-06-24' },
    ],
    milestones: [{ id: 'm1', rowId: 'r1', label: 'Gate 1', date: '2026-06-04' }],
    brackets: [],
    dependencies: [{ id: 'd1', from: 't1', to: 't2', type: 'finish-to-start' }],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

/**
 * The chart, with the gutter and the axis set to zero.
 *
 * That makes client coordinates equal content coordinates, which is the only
 * way to aim a synthetic event in jsdom: `getBoundingClientRect` reports zeros
 * there, so the transform has to be the identity or the test would be asserting
 * against a rectangle the DOM is not reporting.
 */
function Harness() {
  const docNow = useDocumentStore((s) => s.doc);
  const layout = layoutDocument({ doc: docNow, viewStart: VIEW_START, pxPerDay: PX_PER_DAY });
  const svgRef = useRef<SVGSVGElement | null>(null);
  const geomRef = useRef<ChartGeometry>({
    gutter: 0,
    axisH: 0,
    scrollY: 0,
    chartWidth: 800,
    maxScroll: 0,
    layout,
  });
  geomRef.current.layout = layout;
  const { handleSvgMouseDown, dragPreview } = useChartInteractions({ svgRef, geomRef });

  return createElement(
    'svg',
    { ref: svgRef, onMouseDown: handleSvgMouseDown },
    // The same order ChartArea paints, which is now the same order `hitRank`
    // ranks: the connectors' press targets under the bars, their ink over them
    // (SRS-EDIT-077). The stack this test hands `elementsFromPoint` still puts
    // the connector in front regardless, so the ranking is still what is being
    // exercised — paint order is a second lock on the same door, not a
    // replacement for it.
    createElement(DependencyHits, { layout }),
    createElement(TaskBars, { layout, dragPreview }),
    createElement(Milestones, { layout, dragPreview }),
    createElement(Dependencies, { layout }),
  );
}

let root: Root;
let host: HTMLDivElement;

function layoutNow() {
  return layoutDocument({ doc: useDocumentStore.getState().doc, viewStart: VIEW_START, pxPerDay: PX_PER_DAY });
}

function press(el: Element, clientX: number, clientY: number) {
  act(() => {
    el.dispatchEvent(new MouseEvent('mousedown', { bubbles: true, button: 0, clientX, clientY }));
  });
}
function moveTo(clientX: number, clientY: number) {
  act(() => {
    window.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, clientX, clientY }));
  });
}
function release(clientX: number, clientY: number) {
  act(() => {
    window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, clientX, clientY }));
  });
}

const q = (sel: string) => {
  const el = host.querySelector(sel);
  if (!el) throw new Error(`nothing matched ${sel}`);
  return el;
};
const task = (id: string) => useDocumentStore.getState().doc.tasks.find((t) => t.id === id)!;
const milestone = (id: string) => useDocumentStore.getState().doc.milestones.find((m) => m.id === id)!;

beforeEach(() => {
  globalThis.IS_REACT_ACT_ENVIRONMENT = true;
  useEditStore.setState({ editingTaskId: null, snapEnabled: false });
  useViewStore.setState({ showCriticalPath: false });
  useToolStore.setState({ activeTool: 'select' });
  useViewportStore.setState({ startDate: VIEW_START, pxPerDay: PX_PER_DAY });
  useSelectionStore.setState({ items: [] });
  act(() => useDocumentStore.getState().setDocument(doc()));
  host = document.createElement('div');
  document.body.appendChild(host);
  root = createRoot(host);
  act(() => root.render(createElement(Harness)));
});

afterEach(() => {
  act(() => root.unmount());
  host.remove();
  // jsdom does not implement it; a test that stubs it puts it back here.
  delete (document as unknown as { elementsFromPoint?: unknown }).elementsFromPoint;
});

describe('resizing a selected bar (SRS-EDIT-070, SRS-EDIT-071)', () => {
  it('a drag of the end grip moves the end date and leaves the start alone', () => {
    const bar = layoutNow().tasks[0];
    const grip = q('[data-pp-kind="task"][data-pp-sub="end"]');

    press(grip, bar.x + bar.width, bar.y + bar.height / 2);
    moveTo(bar.x + bar.width + 30, bar.y + bar.height / 2);
    release(bar.x + bar.width + 30, bar.y + bar.height / 2);

    expect(task('t1').start).toBe('2026-06-08');
    expect(task('t1').end).toBe('2026-06-20'); // +30px at 10px/day
  });

  it('a dependency painted over the grip does not steal the press', () => {
    // Select it, which this describe always claimed and never did. A grip is
    // only DRAWN on a selected or hovered bar, and since SRS-EDIT-079 an
    // undrawn one no longer outranks ink the planner can see — so without
    // this the test was asserting the grip wins while rendering none.
    act(() => useSelectionStore.getState().setSelection([{ kind: 'task', id: 't1' }]));
    const bar = layoutNow().tasks[0];
    const grip = q('[data-pp-kind="task"][data-pp-sub="end"][data-pp-live="1"]');
    const depHit = q('path[data-pp-kind="dependency"]');

    // What the browser hands the gesture when a connector crosses a bar end:
    // the dependency's transparent 10px stroke in front, the grip behind it.
    (document as unknown as { elementsFromPoint: (x: number, y: number) => Element[] }).elementsFromPoint =
      () => [depHit, grip];

    press(depHit, bar.x + bar.width, bar.y + bar.height / 2);
    moveTo(bar.x + bar.width + 20, bar.y + bar.height / 2);
    release(bar.x + bar.width + 20, bar.y + bar.height / 2);

    expect(task('t1').end).toBe('2026-06-19');
    // And the gesture did not select the connector on the way past.
    expect(useSelectionStore.getState().items.some((i) => i.kind === 'dependency')).toBe(false);
  });

  /**
   * SRS-EDIT-079 — the other half, which is what the owner actually hit.
   *
   * Nothing is selected and the pointer is not on the bar, so no grip is
   * drawn; the connector is the only thing visible at that point. It must be
   * what the press lands on, or a connector crossing any bar end cannot be
   * picked at all — measured at 1.44 px/day, where the chart is narrow enough
   * for the crossing to fall on the connector's clear stretch.
   */
  it('an undrawn grip leaves the press to the connector it crosses', () => {
    const bar = layoutNow().tasks[0];
    const grip = q('[data-pp-kind="task"][data-pp-sub="end"]');
    expect(grip.getAttribute('data-pp-live'), 'nothing is selected, so nothing is drawn').toBeNull();
    const depHit = q('path[data-pp-kind="dependency"]');
    (document as unknown as { elementsFromPoint: (x: number, y: number) => Element[] }).elementsFromPoint =
      () => [depHit, grip];

    const endBefore = task('t1').end;
    press(depHit, bar.x + bar.width, bar.y + bar.height / 2);
    release(bar.x + bar.width, bar.y + bar.height / 2);

    expect(useSelectionStore.getState().items.some((i) => i.kind === 'dependency')).toBe(true);
    expect(task('t1').end, 'the bar was not resized by a grip nobody can see').toBe(endBefore);
  });
});

describe('carrying an element to another row (SRS-EDIT-072, SRS-EDIT-076)', () => {
  it('a bar dragged down lands on the row the pointer is over', () => {
    const before = layoutNow();
    const bar = before.tasks[0];
    const r3Top = before.rowOffsets[2];

    const body = q('.task-bar-body');
    press(body, bar.x + 20, bar.y + bar.height / 2);
    moveTo(bar.x + 20, r3Top + 10);

    // The preview says so before the release does, which is the whole contract
    // of SR-IXC-03: no surprise on letting go.
    const previewed = host.querySelector('.task-bar-body[data-pp-id="t1"]');
    expect(Number(previewed?.getAttribute('y'))).toBeGreaterThanOrEqual(r3Top);

    release(bar.x + 20, r3Top + 10);
    expect(task('t1').rowId).toBe('r3');
  });

  it('a purely vertical drag still commits, though no date changed', () => {
    const before = layoutNow();
    const bar = before.tasks[0];
    const r2Top = before.rowOffsets[1];

    const body = q('.task-bar-body');
    press(body, bar.x + 20, bar.y + bar.height / 2);
    moveTo(bar.x + 20, r2Top + 10);
    release(bar.x + 20, r2Top + 10);

    // The zero-delta guard exists so that a click does not spend an undo frame.
    // A row change is a change with a zero day delta, and it used to be eaten.
    expect(task('t1').rowId).toBe('r2');
    expect(task('t1').start).toBe('2026-06-08');
  });

  it('a drag that stays on its own row leaves the row alone', () => {
    const bar = layoutNow().tasks[0];
    const body = q('.task-bar-body');
    press(body, bar.x + 20, bar.y + bar.height / 2);
    moveTo(bar.x + 50, bar.y + bar.height / 2);
    release(bar.x + 50, bar.y + bar.height / 2);

    expect(task('t1').rowId).toBe('r1');
    expect(task('t1').start).toBe('2026-06-11');
  });

  it('a milestone can be carried too, and follows the cursor while it is', () => {
    const before = layoutNow();
    const ms = before.milestones[0];
    const r2Top = before.rowOffsets[1];

    const diamond = q('[data-pp-kind="milestone"]');
    const startedAt = diamond.getAttribute('points');
    press(diamond, ms.x, ms.y);
    moveTo(ms.x + 60, r2Top + 18);

    // SRS-EDIT-073: the diamond itself moves, not just the date pill. It used
    // to stay nailed to its old date for the whole gesture.
    const live = host.querySelector('[data-pp-kind="milestone"]');
    expect(live?.getAttribute('points')).not.toBe(startedAt);

    release(ms.x + 60, r2Top + 18);
    expect(milestone('m1').date).toBe('2026-06-10'); // +60px at 10px/day
    expect(milestone('m1').rowId).toBe('r2');
  });
});

describe('progress by dragging the fill edge (SRS-EDIT-074)', () => {
  it('sets the percentage from where the pointer lets go', () => {
    const bar = layoutNow().tasks[0];
    // The handle only exists on a bar that is hovered or selected, which is
    // the affordance SR-IXC-13 asks for; selecting is how a test hovers.
    act(() => useSelectionStore.setState({ items: [{ kind: 'task', id: 't1' }] }));

    const handle = q('[data-pp-kind="task"][data-pp-sub="progress"]');
    const mid = bar.x + bar.width * 0.4;
    press(handle, mid, bar.y + bar.height / 2);
    moveTo(bar.x + bar.width * 0.75, bar.y + bar.height / 2);

    // The readout beside the handle, live, for the duration (SR-IXC-06).
    expect(host.querySelector('[data-pp-progress="t1"]')?.textContent).toBe('75%');

    release(bar.x + bar.width * 0.75, bar.y + bar.height / 2);
    expect(task('t1').percentComplete).toBe(75);
    // Nothing else moved: this gesture is not a date edit.
    expect(task('t1').start).toBe('2026-06-08');
    expect(task('t1').end).toBe('2026-06-17');
  });

  it('clamps at the ends of the bar instead of running past them', () => {
    const bar = layoutNow().tasks[0];
    act(() => useSelectionStore.setState({ items: [{ kind: 'task', id: 't1' }] }));

    const handle = q('[data-pp-kind="task"][data-pp-sub="progress"]');
    press(handle, bar.x + bar.width * 0.4, bar.y + bar.height / 2);
    moveTo(bar.x + bar.width + 400, bar.y + bar.height / 2);
    release(bar.x + bar.width + 400, bar.y + bar.height / 2);
    expect(task('t1').percentComplete).toBe(100);
  });

  it('a press that never moves the handle writes nothing', () => {
    act(() => useSelectionStore.setState({ items: [{ kind: 'task', id: 't1' }] }));
    const bar = layoutNow().tasks[0];
    const at = bar.x + bar.width * 0.4;

    const handle = q('[data-pp-kind="task"][data-pp-sub="progress"]');
    press(handle, at, bar.y + bar.height / 2);
    release(at, bar.y + bar.height / 2);

    // 40% as it started: a click is not an edit, and writing the same value
    // back would spend an undo frame on nothing.
    expect(task('t1').percentComplete).toBe(40);
    // And the readout is gone the moment the gesture is (SR-IXC-10).
    expect(host.querySelector('[data-pp-progress="t1"]')).toBeNull();
  });
});
