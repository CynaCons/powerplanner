import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { createElement, act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { Markers, footBandTop, chipWidth, markerDragDate } from '../../src/renderer/Markers';
import { useDocumentStore } from '../../src/stores/documentStore';
import { useSelectionStore } from '../../src/stores/selectionStore';
import { useEditStore } from '../../src/stores/editStore';
import { useViewportStore } from '../../src/stores/viewportStore';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-ELEM-051/052/053/054 — a marker can be reached, moved and renamed.
 *
 * `Markers` drew a `<line>` and a `<text>`, neither carrying `data-pp-kind`,
 * a handler or a hit rect, so nothing about a deadline could be hit: it could
 * be created and then never touched again, and the only thing in the product
 * that could move one was the agent bridge. Owner report 2026-10-29: "Once I
 * have created a deadline marker, I cannot edit the text, I cannot move it
 * around."
 *
 * The drag is exercised as a gesture — mousedown, mousemove, mouseup on the
 * shipped component — rather than through a helper, because the last time this
 * project tested the primary verb through its helpers it shipped nineteen
 * versions with the verb broken.
 *
 * Observed red on 79ccbdb: no `[data-pp-kind="marker"]` node in the markup,
 * the label at y=12 inside row 0, and the marker's date unchanged by a drag.
 */

declare global {
  var IS_REACT_ACT_ENVIRONMENT: boolean;
}

const CHART_HEIGHT = 108; // three 36px rows
/** A window with room to spare, so the band sits below the last row. */
const BODY_HEIGHT = 400;
/** The shape that shipped broken: far more rows than the window can show. */
const TALL_CHART = 1584;
const SMALL_BODY = 726;
const BAND_H = 30;

function doc(): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Markers',
    calendar: { scale: 'day', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [
      { id: 'r1', label: 'Row 1', groupId: null },
      { id: 'r2', label: 'Row 2', groupId: null },
      { id: 'r3', label: 'Row 3', groupId: null },
    ],
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [{ id: 'mk1', type: 'deadline', label: 'Gate review', date: '2026-06-10' }],
    style: { theme: 'light', preset: 'default' },
  };
}

let root: Root;
let host: HTMLDivElement;

function mount(d: GanttDocument, chartHeight = CHART_HEIGHT, bodyHeight = BODY_HEIGHT, scrollY = 0) {
  act(() => {
    useDocumentStore.getState().setDocument(d);
  });
  act(() => {
    root.render(
      createElement(
        'svg',
        null,
        createElement(Markers, {
          doc: useDocumentStore.getState().doc,
          viewStart: '2026-06-01',
          pxPerDay: 10,
          chartHeight,
          bodyHeight,
          scrollY,
        }),
      ),
    );
  });
}

/** Re-render with whatever the store now holds, as ChartArea would. */
function repaint() {
  mount(useDocumentStore.getState().doc);
}

/** Every y the component draws, so a band can be checked for where it landed. */
function drawnYs(): number[] {
  return Array.from(host.querySelectorAll('rect, text, line')).map((el) =>
    Number(el.getAttribute('y') ?? el.getAttribute('y1') ?? 0),
  );
}

function hit(): SVGRectElement {
  const el = host.querySelector('rect[data-pp-kind="marker"]');
  if (!el) throw new Error('the marker has no hit target');
  return el as unknown as SVGRectElement;
}

function marker() {
  return useDocumentStore.getState().doc.markers[0];
}

beforeEach(() => {
  globalThis.IS_REACT_ACT_ENVIRONMENT = true;
  useSelectionStore.setState({ items: [] });
  useEditStore.setState({ editingTaskId: null, snapEnabled: true });
  // The drag reads the viewport the chart draws with, so the test states it
  // rather than inheriting whatever ran before.
  useViewportStore.setState({ startDate: '2026-06-01', pxPerDay: 10 });
  host = document.createElement('div');
  document.body.appendChild(host);
  root = createRoot(host);
  mount(doc());
});

afterEach(() => {
  act(() => root.unmount());
  host.remove();
});

describe('the foot band (SRS-ELEM-053, SRS-ELEM-054)', () => {
  it('sits below the last row while the plan fits the window', () => {
    // The reserved band of SRS-LAY-052: the rows end at 108, the band runs
    // 108..138, and ChartArea's FOOT_BAND_H is what keeps it inside the clip.
    expect(footBandTop(CHART_HEIGHT, BODY_HEIGHT)).toBe(CHART_HEIGHT);

    // Nothing may sit at a negative y or past the reserved band, which is what
    // removed the old bracket rail and the below-row label.
    for (const y of drawnYs()) {
      expect(y).toBeGreaterThanOrEqual(0);
      expect(y).toBeLessThanOrEqual(CHART_HEIGHT + BAND_H);
    }
  });

  it('rides the foot of the window once the plan is taller than it', () => {
    // The regression this test could not see: with 1584px of rows in a 726px
    // body the band was drawn at 1554 — outside the plot clip, so a planner
    // who added a marker saw no chip and no eyebrow at all, and the name was
    // only reachable by wheeling to the foot of a 39-row plan.
    expect(footBandTop(TALL_CHART, SMALL_BODY)).toBe(SMALL_BODY - BAND_H);
    expect(footBandTop(TALL_CHART, SMALL_BODY)).toBeLessThan(TALL_CHART);

    mount(doc(), TALL_CHART, SMALL_BODY);
    const chip = host.querySelector('rect[data-pp-kind="marker"][rx="4"]')!;
    const chipY = Number(chip.getAttribute('y'));
    expect(chipY).toBeGreaterThanOrEqual(SMALL_BODY - BAND_H);
    expect(chipY).toBeLessThanOrEqual(SMALL_BODY);
  });

  it('holds its place as the plot scrolls under it', () => {
    // The chips cancel the parent group's -scrollY, so the band's SCREEN
    // position is the same at every offset. Asserted as the difference, which
    // cannot be satisfied by a band that simply ignores the scroll and is also
    // the one thing a pinned band must do.
    const atTop = footBandTop(TALL_CHART, SMALL_BODY, 0);
    const scrolled = footBandTop(TALL_CHART, SMALL_BODY, 400);
    expect(scrolled - atTop).toBe(400);

    mount(doc(), TALL_CHART, SMALL_BODY, 400);
    const chipY = Number(host.querySelector('rect[data-pp-kind="marker"][rx="4"]')!.getAttribute('y'));
    // Local y moves with the scroll; screen y (local minus scrollY) does not.
    expect(chipY - 400).toBeGreaterThanOrEqual(SMALL_BODY - BAND_H);
    expect(chipY - 400).toBeLessThanOrEqual(SMALL_BODY);
  });

  it('stops each rule at the band, so a line always meets its own chip', () => {
    mount(doc(), TALL_CHART, SMALL_BODY);
    const rule = host.querySelector('line:not([x1="0"])')!;
    expect(Number(rule.getAttribute('y2'))).toBe(footBandTop(TALL_CHART, SMALL_BODY));
  });

  it('names itself, as nothing at the foot of the chart did before', () => {
    expect(host.textContent).toContain('MARKERS');
    expect(host.textContent).toContain('Gate review');
  });

  it('draws no band at all when the document holds no marker', () => {
    const empty = doc();
    empty.markers = [];
    mount(empty);
    expect(host.textContent).not.toContain('MARKERS');
    expect(host.querySelector('rect[data-pp-kind="marker"]')).toBeNull();
  });

  it('sizes a chip to its own name and keeps it hittable when short', () => {
    expect(chipWidth('Gate review')).toBeGreaterThan(chipWidth('Q3'));
    expect(chipWidth('')).toBeGreaterThanOrEqual(44);
  });
});

describe('a marker can be selected and dragged (SRS-ELEM-051, SRS-ELEM-052)', () => {
  it('selects on a press, the way a bar does', () => {
    act(() => {
      hit().dispatchEvent(new MouseEvent('mousedown', { bubbles: true, clientX: 100, clientY: 20 }));
    });
    expect(useSelectionStore.getState().items).toEqual([{ kind: 'marker', id: 'mk1' }]);
    act(() => { window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true })); });
  });

  it('moves the date by the days the pointer travelled, and once', () => {
    const before = useDocumentStore.getState().revision;
    act(() => {
      hit().dispatchEvent(new MouseEvent('mousedown', { bubbles: true, clientX: 100, clientY: 20 }));
    });
    // 10 px/day, so 40px is four days. Three moves, one write.
    act(() => { window.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, clientX: 120 })); });
    act(() => { window.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, clientX: 135 })); });
    act(() => { window.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, clientX: 140 })); });
    act(() => { window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, clientX: 140 })); });

    expect(marker().date).toBe('2026-06-14');
    expect(useDocumentStore.getState().revision).toBe(before + 1);
  });

  it('leaves the date alone when the press does not travel', () => {
    act(() => {
      hit().dispatchEvent(new MouseEvent('mousedown', { bubbles: true, clientX: 100, clientY: 20 }));
    });
    act(() => { window.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, clientX: 101 })); });
    act(() => { window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, clientX: 101 })); });
    expect(marker().date).toBe('2026-06-10');
  });

  it('abandons the move on Escape and writes nothing (SR-IXC-09)', () => {
    act(() => {
      hit().dispatchEvent(new MouseEvent('mousedown', { bubbles: true, clientX: 100, clientY: 20 }));
    });
    act(() => { window.dispatchEvent(new MouseEvent('mousemove', { bubbles: true, clientX: 160 })); });
    act(() => { window.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape', bubbles: true })); });
    act(() => { window.dispatchEvent(new MouseEvent('mouseup', { bubbles: true, clientX: 160 })); });
    expect(marker().date).toBe('2026-06-10');
  });

  it('snaps the committed date to the same grid a bar drag uses', () => {
    // Week grid off the day scale: a four-day pull commits seven days, and the
    // readout drawn during the drag is this value, not the raw delta.
    expect(markerDragDate('2026-06-10', 40, 10, 'month', true)).toBe('2026-06-17');
    expect(markerDragDate('2026-06-10', 40, 10, 'day', true)).toBe('2026-06-14');
    expect(markerDragDate('2026-06-10', 40, 10, 'month', false)).toBe('2026-06-14');
    // Under the threshold a click cannot nudge a date.
    expect(markerDragDate('2026-06-10', 2, 10, 'day', true)).toBe('2026-06-10');
  });
});

describe('a marker renames in place (SRS-ELEM-053)', () => {
  it('opens the inline editor over its chip and commits what is typed', () => {
    act(() => { useEditStore.getState().beginEdit('mk1'); });
    repaint();
    const input = host.querySelector('input.inline-editor') as HTMLInputElement | null;
    expect(input).not.toBeNull();

    // React reads an input through its own value tracker, so the value has
    // to be set through the native setter for the change to register.
    const setValue = Object.getOwnPropertyDescriptor(window.HTMLInputElement.prototype, 'value')!.set!;
    act(() => {
      setValue.call(input, 'Freeze');
      input!.dispatchEvent(new Event('input', { bubbles: true }));
    });
    act(() => {
      input!.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
    });

    expect(marker().label).toBe('Freeze');
    expect(useEditStore.getState().editingTaskId).toBeNull();
  });
});
