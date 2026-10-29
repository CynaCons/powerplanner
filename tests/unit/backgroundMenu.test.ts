import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { createElement, act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { ChartContextMenu } from '../../src/app/ChartContextMenu';
import { useContextMenuStore } from '../../src/stores/contextMenuStore';
import { useDocumentStore } from '../../src/stores/documentStore';
import { useSelectionStore } from '../../src/stores/selectionStore';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-EDIT-060 and SRS-EDIT-061 — a row from the canvas, and no silent no-op.
 *
 * A row could be added from the command bar's Insert group or from an existing
 * row's own menu, both of which need a row to exist and be found first, and
 * from nowhere on the chart — which is where the planner is already pointing
 * when they run out of rows. And on a document with no rows at all, the
 * background menu's own "Add task here" read `doc.rows[0]?.id` and returned:
 * the first gesture a first-time user makes on an empty chart did nothing and
 * said nothing.
 *
 * Observed red on 79ccbdb.
 */

declare global {
  var IS_REACT_ACT_ENVIRONMENT: boolean;
}

function doc(rows: GanttDocument['rows']): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Menu',
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows,
    tasks: [],
    milestones: [],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

let root: Root;
let host: HTMLDivElement;

function openOn(target: { date?: string; rowId?: string }) {
  act(() => {
    useContextMenuStore.getState().show(40, 40, { kind: 'background', ...target });
  });
  act(() => {
    root.render(createElement(ChartContextMenu));
  });
}

function click(label: string) {
  const item = [...host.querySelectorAll('button[role="menuitem"]')].find((b) =>
    (b.textContent ?? '').includes(label),
  );
  if (!item) throw new Error(`no menu item named ${label}; got: ${host.textContent}`);
  act(() => {
    (item as HTMLButtonElement).click();
  });
}

function d() {
  return useDocumentStore.getState().doc;
}

beforeEach(() => {
  globalThis.IS_REACT_ACT_ENVIRONMENT = true;
  useSelectionStore.setState({ items: [] });
  host = document.createElement('div');
  document.body.appendChild(host);
  root = createRoot(host);
});

afterEach(() => {
  act(() => root.unmount());
  host.remove();
  act(() => useContextMenuStore.getState().close());
});

describe('a row from the chart (SRS-EDIT-060)', () => {
  beforeEach(() => {
    act(() => {
      useDocumentStore.getState().setDocument(
        doc([
          { id: 'r1', label: 'BSW', groupId: null },
          { id: 'r2', label: 'MCAL', groupId: null },
        ]),
      );
    });
  });

  it('offers one, and lands it under the row that was clicked', () => {
    openOn({ date: '2026-06-10', rowId: 'r1' });
    click('Add row below');
    const ids = d().rows.map((r) => r.id);
    expect(ids.length).toBe(3);
    expect(ids[1]).not.toBe('r2'); // the new row sits between r1 and r2
    expect(ids[0]).toBe('r1');
    expect(ids[2]).toBe('r2');
    // And it is selected, so the next gesture acts on it.
    expect(useSelectionStore.getState().items).toEqual([{ kind: 'row', id: ids[1] }]);
  });

  it('keeps a new row in the section its neighbour belongs to', () => {
    act(() => {
      useDocumentStore.getState().setDocument(
        doc([
          { id: 'sec', label: 'Phase 1', groupId: null },
          { id: 'r1', label: 'BSW', groupId: 'sec' },
        ]),
      );
    });
    openOn({ date: '2026-06-10', rowId: 'r1' });
    click('Add row below');
    expect(d().rows[2].groupId).toBe('sec');
  });
});

describe('an insert on an empty chart creates what it needs (SRS-EDIT-061)', () => {
  beforeEach(() => {
    act(() => {
      useDocumentStore.getState().setDocument(doc([]));
    });
  });

  it('adds a task where there was no row to put it on', () => {
    openOn({ date: '2026-06-10' });
    click('Add task here');
    expect(d().rows.length).toBe(1);
    expect(d().tasks.length).toBe(1);
    expect(d().tasks[0].rowId).toBe(d().rows[0].id);
    expect(d().tasks[0].start).toBe('2026-06-10');
  });

  it('adds a milestone the same way', () => {
    openOn({ date: '2026-06-10' });
    click('Add milestone here');
    expect(d().rows.length).toBe(1);
    expect(d().milestones.length).toBe(1);
    expect(d().milestones[0].date).toBe('2026-06-10');
  });

  it('still offers a row of its own on an empty chart', () => {
    openOn({ date: '2026-06-10' });
    click('Add row');
    expect(d().rows.length).toBe(1);
  });
});
