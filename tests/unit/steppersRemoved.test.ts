import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { createElement, act } from 'react';
import { createRoot, type Root } from 'react-dom/client';
import { SelectionGroup } from '../../src/app/SelectionGroup';
import { useDocumentStore } from '../../src/stores/documentStore';
import { useSelectionStore, type Selectable } from '../../src/stores/selectionStore';
import type { GanttDocument } from '../../src/types/document';

/**
 * SRS-EDIT-075 — no stepper for a date or for a percentage.
 *
 * SR-IXC-01 forbids "stepper buttons, +/- incrementers, or similar discrete
 * controls for continuous values" in those words, and the command bar shipped
 * four of them: −1d, +1d, −10%, +10%. They were against the product's own
 * interaction spec from the day they shipped, and the owner found them the
 * only way to set progress at all: "Right now the progress completion, I can
 * only do it with the plus 10, plus 20."
 *
 * The replacement is not another control. It is the two drags in SRS-EDIT-072
 * and SRS-EDIT-074, with the details dialog keeping precise numeric entry —
 * which is the secondary editor SR-IXC-02 reserves for exactly that.
 *
 * Observed red on 79ccbdb: four stepper buttons in the bar, two on a task and
 * two on a milestone.
 */

declare global {
  var IS_REACT_ACT_ENVIRONMENT: boolean;
}

/** Every label and tooltip a stepper has ever worn in this bar. */
const STEPPER_TEXT = ['−1d', '+1d', '-1d', '−10%', '+10%', '-10%', 'Ten percent', 'a day earlier', 'a day later'];

function doc(): GanttDocument {
  return {
    schemaVersion: 1,
    title: 'Bar',
    calendar: { scale: 'week', fiscalYearStart: 1, workingDays: [1, 2, 3, 4, 5], holidays: [] },
    rows: [{ id: 'r1', label: 'Row 1', groupId: null }],
    tasks: [
      { id: 't1', rowId: 'r1', label: 'Architecture', start: '2026-06-08', end: '2026-06-17', percentComplete: 40 },
    ],
    milestones: [{ id: 'm1', rowId: 'r1', label: 'Gate 1', date: '2026-06-04' }],
    brackets: [],
    dependencies: [],
    markers: [],
    style: { theme: 'light', preset: 'default' },
  };
}

let root: Root;
let host: HTMLDivElement;

function show(selection: Selectable[]) {
  act(() => {
    useSelectionStore.setState({ items: selection });
  });
  act(() => {
    root.render(createElement(SelectionGroup));
  });
}

/** Everything a press could be aimed at, label and tooltip together. */
function pressables(): string {
  return Array.from(host.querySelectorAll('button'))
    .map((b) => `${b.textContent ?? ''} ${b.getAttribute('title') ?? ''} ${b.getAttribute('aria-label') ?? ''}`)
    .join(' | ');
}

beforeEach(() => {
  globalThis.IS_REACT_ACT_ENVIRONMENT = true;
  act(() => useDocumentStore.getState().setDocument(doc()));
  host = document.createElement('div');
  document.body.appendChild(host);
  root = createRoot(host);
});

afterEach(() => {
  act(() => root.unmount());
  host.remove();
});

describe('the command bar carries no steppers (SRS-EDIT-075)', () => {
  it('offers none on a selected task', () => {
    show([{ kind: 'task', id: 't1' }]);
    const text = pressables();
    expect(text, 'the bar drew nothing for a task').not.toBe('');
    for (const s of STEPPER_TEXT) expect(text).not.toContain(s);
  });

  it('offers none on a selected milestone', () => {
    show([{ kind: 'milestone', id: 'm1' }]);
    const text = pressables();
    expect(text).not.toBe('');
    for (const s of STEPPER_TEXT) expect(text).not.toContain(s);
  });

  it('keeps the door to precise numeric entry open', () => {
    // The steppers go because dragging replaces them, not because the numbers
    // stop mattering: SR-IXC-02 keeps a secondary editor for exact values, and
    // this button is the way in.
    show([{ kind: 'task', id: 't1' }]);
    expect(pressables()).toContain('Task details');

    show([{ kind: 'milestone', id: 'm1' }]);
    expect(pressables()).toContain('Milestone details');
  });
});
